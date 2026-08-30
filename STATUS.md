# Status

A dated snapshot of where the reconstruction is, kept so a session does not
have to re-derive it. **`CLAUDE.md` and `docs/` are authoritative**; this file
is a summary and can be stale between updates. Every number below carries the
command that produces it, so it can be re-measured rather than believed.

Last updated: **2026-08-29**, at `f64eade`. Working tree clean.

## In flight

Nothing uncommitted. **1,190 patches.**

Sixty-eight functions since the last snapshot. The seven-class HUD family is
complete, the radar is reconstructed end to end, and CLAUDE.md's Lock/Unlock
batch has gone from **14 to 25 of 29** -- four left, and none of them small.

- **`DrawSelection`** (`0x00462120`, 688 B) is the newest: the caret over the
  army's leader and a health bar under every selected unit, and the pruning of
  `ADDR_SELECTED_UIDS` that happens *while* it draws. One lock and two
  unlocks, because only the caret goes into locked bits and every bar is a
  `ClearRegion`, which blits.

  It also corrected a written claim. `RefreshDraw` carried "no drive here
  produces" the arm that reaches it; a probe says an ordinary Boot Camp start
  runs it **55 times**, all at state 2, all through `TakeMenuRequest`, and all
  while the two opening dialogs are up. `RefreshScreen` -- the other caller --
  still reads 0, and now for a mapped reason rather than a shrug: its six
  dialog-opener callers each require `ADDR_GAME_STATE` 2, so opening AUDIO
  from the title screen cannot reach it.

  And measured in the other direction too: displacing every bar 40 pixels
  leaves `bootcamp` at 22 and `mission` at 281 against 287. The suite runs
  this function and does not discriminate it.

- **`AirFrameDraw`** (`0x00409070`, 864 B) is the twenty-second: the
  air-support run -- an aircraft on a three-leg flight path, the gauge that
  times it, and the frame cycle of the object that called it in, each on its
  own millisecond timer. The path is not in the function; nine one-line
  derivations at `0x00408AB0..0x00408D16` compute it, and reading those is
  what turned transcription into description.

  It corrected three field types and one open question. `AIR_OFF_FLAG_A`,
  `_FLAG_B` and `AIR_OFF_ACTIVE` are milliseconds, not flags. And two path
  constants are written at startup, so the values in the image file are not
  the ones the game uses -- a table dumped out of the binary answers about
  the file.

- **`MovieBuildName`** (`0x0042E770`, 592 B) is small and settled something
  that had been open for months. It appends `sml` to a movie's filename when
  `ADDR_OPT_MUSIC` is 0 and the machine is slow -- so that flag is
  **`ADDR_OPT_BIG_MOVIES`**, `-bm`/`-sm` are big and small movies, and
  `SetGameDir` latching it on entering `avi` means the full-size set is
  present. CLAUDE.md had recorded the two readings and said one must be
  wrong; both came from functions that WRITE the flag. Reading one that uses
  it took minutes.

  `tools/moviecheck.py` is the nineteenth tool in `make check`: 64 cases, the
  whole input space, run against the original under Unicorn in a tenth of a
  second. Three mutations of the expected rule fail it, one of them on
  exactly the single case that distinguishes it.

- **`PlayMovie`** (`0x0042E5E0`, 592 B) went in beside it and closes the movie
  state entirely: every call `StateEnter3` makes is now ours. It ran **twice**
  on a `-dbg` startup with `MoviePoll` at 618,232, and `MovieOpen`,
  `MovieSetVolume`, `MovieSetCurrent` and `MovieStart` all dropped to 0 in the
  same run -- the blind spot arriving exactly where reconstruction predicts,
  which is the confirmation rather than the worry.

  `-nm` is **no movies**: two references in the image, the switch parse and
  this function's gate.

- **`CommReopenSession`** (`0x0040FA00`, 152 B) puts a finished multiplayer
  game back in the lobby, and the two bits it clears are what name it:
  `AM2_SESSION_FLAGS_START` (0x21) is `DPSESSION_NEWPLAYERSDISABLED |
  DPSESSION_JOINDISABLED`, OR'd in when the game starts, and this is exactly
  that undone. Verified by reading -- it needs a session that has ENDED,
  which needs a second player.

  Second loop this stretch that does not advance over a removal, after
  `DrawSelection`. It terminates only because the callee changes what is at
  that index, which is a property of the callee; the code alone reads like a
  hang, so it says so.

- **`DrawEffectLayer`** (`0x004123D0`, 992 B) is the AIM MARKERS: one entry
  per army, each a 112x112 **refraction** of the offscreen surface -- every
  pixel taken from wherever a {dx,dy} table points, nothing blended -- with
  two sprite pairs over it. Bracket batch **27 of 29**, and the two left are
  1088 and 2656 bytes.

  It corrected a written claim on the way. `orig.h` said the table was
  "records of 0x64 bytes"; the ager handles BOTH tables in one unrolled body,
  so `[eax]` and `[eax+0x64]` read exactly like a stride. `add eax, 4` at the
  bottom says otherwise. **Take a stride from the loop step, never from the
  largest displacement in the body.**

  Both animation divisors were measured by running the multiply-shift: 50 ms
  for one clock and **150** for the other, where reading the magic constants
  by eye would have given 100 and 50.

  Not one entry is ever live on any drive here -- eight dwords read over the
  socket through a firing Boot Camp mission, all zero -- so the clean A/B
  checks the loop guard and nothing else. `ADDR_AIM_START` is one arm of a
  42-arm weapon dispatcher.

- **`SeqAddKind5` and `SeqAddKind7`** (`0x00462000`, `0x00462080`) were
  `ADDR_BY_REF_ACTION_A` and `_B` -- names taken from the one thing their
  call sites showed, a point passed by reference. Their bodies say what they
  do: add one 48-byte SEQ record at a map point. Three seams closed across
  `event.cpp` and `item.cpp`, and the tree's `orig_` count is down to 128.

  **Name a function from its body, not from its argument list.** That is the
  same rule as naming from the body rather than from a call site, one step
  further in -- "by ref" was true and said nothing.

- **`WeaponPoseIndex`** (`0x004494A0`, 208 B) picks the pose a unit takes for
  the weapon it holds, out of a 43-byte table transcribed by hand.
  `tools/posecheck.py` is the twentieth tool in `make check` and checks all
  282 cases against the original -- and its **mutation counts** are the
  useful part: one corrupt table byte fails 2 cases, swapping two constants
  fails 5, dropping an armed branch fails 8, and 5 and 8 are exactly how many
  codes select those arms. A failure count that matches the table is evidence
  the corpus reaches every arm; "0 disagree" never is.

  It also caught a comment being half right: `orig.h` said this function
  "computes an index into `ADDR_WEAPON_POSE_FRAMES`" -- true of what it
  returns, and not of the table its default arm READS, which is 416 bytes
  earlier and is now `ADDR_POSE_BY_CLASS`.

- **`SendVehicleExit`** (`0x0045E3C0`, 192 B) tells the other players a unit
  has got out of a vehicle. `ADDR_VEHICLE_DROP_OCCUPANT` is half the story --
  it drops nobody, it REPORTS a drop -- and the name is kept with the
  correction in its comment rather than aliased, which is what the ratchet is
  for.

  It also settles something about a hundred call sites: `UidOnWire` is
  `mov eax, [esp+4]; ret`, the identity. This function converts twice on its
  outgoing log line and nothing happens.

- **`VehicleMsgRecv`, `RecvVehicleExit` and `VehicleTakeOutOccupant`** close
  the other end of it: the vehicle half of the army-message dispatcher and
  the receive path for kind 0x25, so that message is now ours at **both**
  ends. `VehicleTakeOutOccupant` is the exact mirror of what `army.cpp` does
  locally, and every field it touches was already named from that side --
  `OBJ_OFF_RIDING`'s own comment says "cleared as an occupant gets out", and
  this is the other place that does it.

  Four of the dispatcher's eleven arms are the unknown-message log, and that
  is corroboration rather than a hole: `orig.h` already had 0x22 as
  "handled somewhere else entirely" and 0x23 as `AM2_MSG_DEATH`. Two message
  families share one number space.

- **`TroopMessageRecv`** (`0x0044C590`, 240 B) is its sibling, and it gave
  back two message names in **the program's own vocabulary**: kind 0x21 logs
  "got eTROOPER_DROP_ITEM_MESSAGE" and 0x22 "got
  eTROOPER_SET_WEAPON_MESSAGE". The `e` prefix is the original's enum
  convention, so those two codes now carry the names their authors used. 0x22
  also closes the note that called `AM2_MSG_TROOPER_WEAPON` "handled
  somewhere else entirely" -- this is somewhere else.

- **`SeqRun`** (`0x00461870`, 192 B) finishes the seq family: an
  index-chained walk where each stepper returns the next index. Two
  corrections to `orig.h` from reading it -- the gate is skipped when it is
  **zero**, not "not positive" (`test r,r; jbe` is `jz`, the rule this file
  already records), and `SEQ_OFF_NEXT` is an **int16** the adders happen to
  clear as a dword.

  **Arm 1 is an infinite loop, and it is reproduced.** Kind 1 jumps to the
  loop test with the index unchanged, so a live record of that kind hangs the
  game. `orig.h` called it "does nothing but continue"; continuing needs a
  new index and that arm supplies none.

  Measured: the list is EMPTY on every drive here -- count 0 with the array
  allocated and capacity 200 -- so stepping no record at all changes nothing.
  That is "there are no records", not "the steppers do not matter".

- **`AimMarkerAge`** (`0x00412190`, 272 B) is the other half of the aim
  markers, and writing it CONFIRMED the correction it caused. Every offset it
  uses lands exactly on a name given to those parallel arrays three commits
  ago -- +0x00, +0x10, +0x20, +0x30 for one table and +0x64 through +0xA4 for
  the other. The "records of 0x64 bytes" reading could not have produced that.

  Two asymmetries reproduced: the deadline is tested twice for the local
  player (dead the second time, load-bearing for everybody else), and the
  shared expiry clears the random frame where the local one does not.

  **`ab.sh` refused a run rather than passing it**, reporting "no game log
  lines" on the recon side. The build was fine -- launched by hand it reaches
  the title screen with 1,088 patches -- so the guard fired on a launch
  failure and did exactly what it was added to do. Re-run clean.

- **`SeqStepKind2` and `SeqAddKind6`** (`0x00461310`, `0x00461660`) take the
  seq family to five of its eleven functions. Kind 6's sprite is a **variant
  of eight frames**, not a frame -- it shares kind 7's array and indexes it by
  `variant * 8` from a global.

  And it turned `ROW_OFF_FIELD_26` from "a scale or a count" into a likely
  **depth key**: kinds 5 and 7 write 1000 and 1, and kind 6 writes the scaled
  terrain attribute under its own point plus 1010. A ground-height term in
  the middle of three constants reads as a sort order. Recorded as a reading;
  nothing yet shows what consumes it.

- **`SeqStepKind5`** (`0x004614D0`) makes kind 5 an **emitter**: every 300 ms
  it adds a kind 4 at its own point, jittered -4/0/+4 in x, into the *other*
  context. Two more field corrections fall out of it.

  `SEQ_OFF_GATE` **is not a flag** -- this stepper adds the frame delta to it
  every frame and subtracts the interval when it passes, so it is a
  millisecond accumulator. The adders' `= 1` means "start just above zero so
  the walker does not skip me".

  And `ROW_OFF_STAMP_54` is **milliseconds here and a frame count in kind
  2**. One field, two units, chosen by the kind that owns the record -- there
  is no single reading that is right for both.

- **`SeqAddKind4`** (`0x00461350`) is the fourth adder, the only one filling
  the second context, and what a kind 5 emits. It **identifies
  `ROW_OFF_FIELD_2C`**: three adders zero it and this one writes
  `ADDR_REMAP_SHADES[0]`, and `MAPOBJ_OFF_LUT` is already 0x2C "-> the
  sprite's +0x34". So it is the row's remap table and a kind 4 is a shaded
  sprite. **A field that three writers agree is zero says nothing; the fourth
  writer is what names it.**

  It also shows `MAPOBJ_*` and `ROW_OFF_*` are two name families for one
  structure -- 0x00 and 0x2C are both, with the same meanings. Recorded, not
  merged; collapsing two families is a change of its own.

- **`SeqStepKind4`** (`0x004613E0`) makes the pair legible: a kind 4 is a
  four-cell shaded sprite that drifts 3 right, 1 up and 3 further off the
  ground every 120 ms, advancing cell after a per-cell hold and retiring on
  its last cell or on leaving the map.

  Two parallel tables read with ONE index -- the sprite array and
  `ADDR_REMAP_SHADES` -- which is what says the four shade tables are an
  animation rather than four independent effects.

  Seven of the seq family's eleven are ours, and the pattern across them is
  that **nothing in a seq record means one thing**: `SEQ_OFF_LIFE` is an
  interval here and a total in kind 5, `SEQ_OFF_GATE` a step counter here and
  a millisecond accumulator there, `ROW_OFF_STAMP_54` milliseconds in one
  stepper and frames in another.

- **`ReadWaveFile`** (`0x0040C340`) is the first of this stretch with a
  BYTE-EXACT oracle. `tools/checkwaves.py` compares what reaches every
  DirectSound buffer against the `.WAV`'s own data chunk: **56 waves, 0
  differ**.

  And its sensitivity was measured, not assumed. Reading `size - 1` bytes
  fails only **2 of 56** -- a wave whose data chunk does not run to the last
  byte of the file never notices the missing one. So the check catches a
  truncation only where the file has no trailing slack, and it took a
  mutation to find that out rather than the 56/56.

  It leaks the file handle on two of its four failure paths. The original's,
  reproduced -- the game opens 56 waves once at startup and never retries, so
  a leak that cannot accumulate is not the same defect as one that can.

- **`PlaceObj`** (`0x00429220`) is the deploy dispatcher's default arm. Its
  second interesting fact is a **confirmation**: row 0 goes to the point and
  every other row to the point plus its sprite's attach offset, which is the
  second independent reader of `AM2_Sprite::attachX/attachY` and agrees with
  the first.

  Its early exit needs the position unchanged AND the flag SET, and the body
  clears that flag on the way out -- so the flag means "already placed" here.

  Coverage stated rather than implied: it runs **once per mission**, through
  `EvtDeployItem`. Four clean configurations compare it on a handful of
  calls, not on the map load.

- **`ApplyHeightItem`** (`0x00433C20`) is the height handler for types 1 and
  4, and it **settles the depth-key reading**: it writes
  `ScaleBy32Blocks(height) - 1000` into `ROW_OFF_FIELD_26` and then adds an
  int16 from the object's def record. A height term plus a per-type constant,
  summed into one field the renderer reads -- that is a depth key, and the
  record's field is what lets two types at the same height sort against each
  other. Third independent writer, and the first that makes it unambiguous.

  Its chain walk **stops** at the first link that is not an item rather than
  skipping it, which matches `ApplyObjFrame` two hundred lines up -- the
  family's convention, not this function's accident.

  A frame-count FAIL of 297% on `campaign` re-ran clean, as the two before
  it did.

- **`RecvTroopBatch` and `RecvTroopPair`** (`0x0044CC90`, `0x0044C960`) close
  two arms of the trooper dispatcher. Kind 0x16 is a **batch** -- a run of
  variable-length sub-records, each parser returning the pointer past itself
  -- which is why it is the one arm that takes the army.

  Kind 0x18's receiver **confirms its sender**. `armymsg.cpp`'s
  `SendPairMsg` was reconstructed first with the note "nothing read so far
  says what the pair means"; the receiver validates the field layout exactly,
  settles the argument order (+0x18 before +0x14, byte before field), and
  says the second object must be a **type 4 -- a weapon**.

  **The offset ratchet caught me** adding a second name for all four of that
  message's fields. `MSG_PAIR_OFF_*` already had them from the sender.

  `combat` reported **177,112 pixels (22.5%)** and `ab.sh` still said "A/B
  clean", because that configuration's budget is disabled. Re-ran at 716.
  Read the number, not the verdict.

- **`RecvTrooperSetWeapon`** (`0x0044C3E0`) makes kind 0x22 ours at both
  ends, and it **names the sender's last unnamed field**: `SendTrooperSetWeapon`
  wrote a literal `msg + 0x18` and could only call it "the weapon"; the
  receiver uses it twice as an INDEX, so it is an inventory slot.

  Its three log lines are **not** gated on `COMM_OFF_VERBOSE`, unlike the
  vehicle-exit pair -- an asymmetry inside one message family. And the
  success line fires before either lookup, so it announces a link the next
  branch may refuse.

  `checkseams` failed the build: I reached for the image for
  `SoldierKindForWeapon`, which is already ours. Three of this function's
  four callees were reconstructed and I made a seam to one out of habit.

- **`combat`'s pixel figure is bimodal** -- 177,112 / 716 / 177,109 / 684
  across four runs of one build, three pixels apart within each cluster.
  That is the two sides' scroll being in or out of phase, not noise. Recorded
  in CLAUDE.md; its budget is disabled and the log is the evidence.

- **`StepRowAnim`** (`0x0040A380`) is the per-row animation advance -- once
  per row of every object every frame -- and it is the first reconstruction
  this session that **shipped a defect the A/B caught**.

  One missing dereference: `ADDR_SPRITE_LIST` holds the array, and I read the
  global as the array. The game left at the first mission. That is the exact
  failure `CLAUDE.md` records under "obj -> table -> slot", which I had read.

  **What caught it was the widget dump, not the pixels.** `campaign` came
  back missing a whole second dump and 294,304 pixels -- and the same figure
  to the pixel on a re-run, which is what said "deterministic, not timing".
  A repeated exact number is the signal; a repeated approximate one is not.

  It also settles `ROW_OFF_FRAME`: below 1000 a frame id, exactly 1000 "take
  up the queued animation", above 1000 a countdown in frames. One field, three
  meanings by range.

- **`ObjectsAtPoint`** (`0x0042A550`, 15 callers) is `ObjectsHitByPoint`'s
  sibling and asks a **looser** question of the same cell: the hit rectangle,
  then one of three further tests -- the object's rows' sprites, a box built
  from four offsets nothing else in the image reads, or the single bitmask
  the sibling uses. So an object with no mask is accepted by a BOX here and
  by its rectangle alone there. Not duplicates.

  Coverage stated: the sibling's own comment records that returning NULL
  unconditionally left `mission` and `bootcamp` at their floors across 3,872
  calls. Fifteen callers is not evidence anything watches the answer.

- **`RankPromote`** (`0x00457BC0`) bumps the rank and, for a plain type 2 at
  ranks 3, 5 and 7, hands over a new weapon -- 0x0A, 0x08, 0x1D, in that
  order, with ranks 4 and 6 giving nothing.

  **The rank is written before the weapon's guards are tested**, so a
  vehicle, a Sarge or a soldier of a non-zero kind is still promoted and just
  gets nothing for it. Reading this as "promote a trooper" would put the
  write inside the guard and quietly stop ranking everything else.

  `checkoffsets` refused an identical redefinition of `AM2_RANK_MAX` -- I
  defined it a second time where it already sat beside `OBJ_OFF_RANK`.

- **`StepType8`** (`0x0043D980`) is the roach's per-frame step, and it turned
  up a discrepancy worth keeping: **it dispatches on `OBJ_OFF_FIELD_530` and
  writes `OBJ_OFF_DEATH_STATE`**, which are different fields carrying the same
  {0, 5, 6} vocabulary. `DamageRoach` writes 0x554 and never touches 0x530 --
  checked against the bytes, because two fields sharing one vocabulary is
  exactly the shape a mis-transcribed offset produces. What sets 0x530 is not
  established.

  Its five unnamed callees got **role names from where they sit in this one
  function**, which is the weakest kind of naming here and is labelled as
  such.

  Closing the seam pulled `anim.cpp` into `SELFTEST_SRC` and that pulled in
  three win32 sprite loaders. Adding `win32/sprite.cpp` was tried first and
  is not an option -- it needs ddraw, and the harness exists so that no part
  of the game runs. Three stubs, with the reasoning beside them.

- **`ItemPostCreate`** (`0x0043A210`) found a **live defect in committed
  code**. `orig.h` had it as `void(obj, int32)`; the function opens
  `cmp eax, 4; jge` and returns, so its first argument is an ARMY.
  `RecvItemCreate` had been passing the freshly created object into that slot
  -- every call returned at the first instruction and no tile was ever
  revealed.

  It survived because nothing here can run it: `RecvItemCreate` is a
  multiplayer receiver. **A wrong signature on a `void` function called
  through a pointer costs nothing until somebody plays a network game.**
  Found by reading the callee, which is the only thing that could have found
  it.

  It also reads `ADDR_MAP_TILES_H` for the initial index and
  `ADDR_MAP_TILES_W` for the row stride -- which agree only on a square map.
  A fourth reading of that contested pair, and it fits neither way round.
  Reproduced exactly.

- **`Type2ActionAll`** (`0x00417AB0`) is the THIRD loop in this tree that does
  not advance over a removal, after `DrawSelection` and `CommReopenSession`.
  The unresolved arm jumps past the increment, so the entry that shifts down
  is looked at next; the bound is re-read from the list each iteration, which
  is what makes that safe. Three instances make it a pattern rather than a
  pair.
- **`FileHasSaveTag`** (`0x00423620`) accepts TWO tags, and does not check
  that the read succeeded -- a file that opens and gives fewer than four bytes
  is answered from whatever the stack held. Its callers hand it directory
  entries, so that is reachable. The original's, reproduced.
- **`DumpMsgList`** (`0x004013B0`) lives in `win32/dplay.cpp` rather than
  beside the rest of the list code in `msgslot.cpp`, because the flat half
  cannot name `HANDLE`. The split decides where a function goes even when
  every function it belongs beside is on the other side.

- **`ADDR_SOLDIER_NAMES` was pointing at the wrong column, and it took a
  second reader to show it.** The table is 62 records of
  `{const char *name; int32_t taken}` at `0x00489BF8`, and the macro was
  `0x00489BFC` -- the taken flag. `TakeSoldierName` only ever touches that
  flag and the stride is 8 either way, so it indexed correctly and the error
  was invisible for as long as it had one user. `SoldierNameOf`
  (`0x004475C0`) needs the name at +0 of the same record and would not
  compile as a lie. **One consumer cannot check a layout.**

  The names are the team's own -- "D. DuBois", "J. Wildblood", "One Eye".
- **`MSGNODE_OFF_OWNER` was likewise named from a dump.** `DumpMsgList`
  prints the dword at +8 of whatever +0x20 holds, which reads as an owner
  record; `MsgListCopyByKey` (`0x004012C0`) memcpy's it wholesale for the
  length at +0x24, so it is the message BODY. Renamed with +0x14 to
  `MSGNODE_OFF_KEY` at the same time, which is what that function matches on.
- **`RandomPointAhead`** (`0x00404ED0`) ignores its first argument -- both
  call sites push four and the body uses three. Its heading is also passed as
  a dword with three uninitialised bytes above the one the original computes;
  `Cos8` and `Sin8` mask their index, which is why it has never mattered.

- **`WeaponClassOf`** (`0x0042AAE0`) has a four-arm jump table whose arms are
  NOT in the order they are laid out. Reading the bodies top to bottom gives
  1, 2, 3, 4 for kinds 2, 3, 4, 5; the table at `0x0042AB38` dispatches them
  2, 3, 1, 4. Second instance in this project after the state-2 sub-state
  table -- **take the order from the table, never from the addresses**, and
  it is worth one look every time.
- **`DamageItemChain`** (`0x00435650`) and `DamageItem` (`0x004356C0`) are
  mutually recursive, and the recursion is bounded by an ARGUMENT rather than
  by either body: every call from the chain walker passes 1 for DamageItem's
  sixth parameter, and DamageItem's first test is on exactly that -- non-zero
  takes the arm that does not come back. Changing the constant would make it
  unbounded.
- **`AwardOwnArmyXp`** (`0x00417B10`) is the FOURTH non-advancing removal loop
  in this tree, and it indexes `ADDR_ARMY_OBJ_LISTS` element 0 with no slot
  lookup at all, where every other walker resolves one first. Same thing in
  single player; not necessarily in a multiplayer game. Reproduced.
- **`ObjOverlayY`** (`0x0044A3C0`) indexes the animation's cell array by
  DIRECTION alone. The cells are `frames * directions`, direction-major, so
  `cells[dir]` is frame `dir` of direction 0 rather than frame 0 of direction
  `dir` -- the same thing only when there is one frame. Reproduced as written;
  a "fix" here would move what is drawn.

- **`MsgListTakeFlags`** (`0x00401330`) is `MsgListCopyByKey` with the key
  test replaced by a mask test -- and it CONSUMES what it finds, clearing the
  matched bits on the node before copying the body out. That is what stops a
  second call answering the same node, so it cannot be written as a pure
  find. The mask is a global rather than an argument, so two calls with the
  same list can differ for that reason alone.
- **`RandomPointToward`** (`0x00404E50`) and `RandomPointAhead` differ in one
  line: where the heading comes from. Both add the same `(rand & 0x3F) - 32`
  spread. `AngleBetween` is called obj-then-target and the step is from obj,
  which is the only ordering that approaches anything -- and both arguments
  are the same type, so nothing would catch the swap.
- **`FreeScenarios`** (`0x0043DD30`) skips its two global clears when the
  table is NULL, because the original tests the pointer before them. Nothing
  observable turns on it -- the count is already 0 in that case -- and it is
  reproduced rather than tidied.

- **Two map grids are named from evidence at last, and a +1/-1 pair is what
  did it.** `0x00514EC0` is the map's CELL WEIGHTS: `ObjClearFootprint` does
  `add byte, 0xF1` on it -- subtract fifteen -- per footprint point, and
  `MarkOpenTile` tests it against exactly that fifteen. `0x00514EE8` is a
  per-tile COVER COUNT, and what settles it is that its only two writers are
  an exact `inc`/`dec` pair over one tile and twenty neighbours.
  `ObjClearFootprint` calls the second right after taking fifteen off the
  first, so the two move together and count different things.

  Neither could be named from any single reader. **A table with one consumer
  is a table you cannot name**, which is the same lesson `ADDR_SOLDIER_NAMES`
  taught one batch earlier.
- **Three functions sharing a neighbourhood do NOT share its precondition.**
  `TileCoverAdd` and `TileCoverSub` refuse a tile outside a two-tile margin,
  and then check no individual neighbour -- the margin is what makes the walk
  safe. `MarkOpenTile` walks the same twenty deltas with NO margin test at
  all, so near an edge it reads outside the grid. It was nearly written with
  the test, on the assumption that the three were of a piece. Read each one.
- **`LoadTilesetPalettes`** (`0x0042B120`) is gated entirely on
  `ADDR_TILESET_RESERVE`: a zero there does not select a loading mode, it
  means load nothing. Its six file-name strings are also laid out BACKWARDS --
  `palette5.bmp` at the low address -- so the natural forward walk reads past
  `palette0.bmp` and hands the result to `fopen`.
- **A pixel figure moved and the re-run put it back.** This batch's first
  `bootcamp` read 76 rather than its usual 22, and `campaign` 47 rather than
  2 -- both far inside budget, which is exactly the case CLAUDE.md warns can
  hide a real difference. The same build re-run gave 22 and 2. **Read the
  number even when the verdict is clean, and re-run before believing it.**

- **`RowAnimField4`** (`0x0040A130`) is the reader `ROW_OFF_FIELD_2C`'s block
  in `orig.h` predicted from the writing side, and it arrived matching: the
  value is doubled exactly when the row's lut is `ADDR_ROW_LUT_DOUBLES`, and
  the lut is compared BY ADDRESS rather than by anything in it. Its first test
  is a shortcut that does not go through the table at all -- an id equal to
  the row's own frame answers the cached `ROW_OFF_FIELD_3C` and is NOT
  doubled, so the same id gives two answers depending on whether the row
  happens to be showing it. **A prediction written down from one side and met
  from the other is better evidence than either reading alone.**
- **`ObjBoxAction`** (`0x00438F80`) returns 0 and 1 that are not failure and
  success: 1 is "nothing to do", 0 is "no sprite at all", and the real answer
  is the callee's. It also tests sprite flags `0x1C` where `sprite.h`
  documents the software mask as `0x3C`, so bit 5 alone takes the do-nothing
  arm. Both stated, because `0x3C` is the number a reader arrives with.
- **`SlotBandHeading`** (`0x00456E20`) halves the slot to get an index, so two
  consecutive slots share one -- and the pair is told apart by the parity of
  the SLOT, not of the index. The two disagree for exactly the values the
  function exists to separate.

- **`SelectBestWeapon`** (`0x004069B0`) guards five of its six slots and not
  the first: slot 0 is looked up and dereferenced unconditionally, so an empty
  one goes through `WeaponByUid` -- which complains and answers NULL -- and
  the dereference takes the process down. Reproduced; adding the guard would
  be inventing a behaviour. It also uses `WeaponByUid` rather than
  `HeldWeaponCode`'s quieter lookup, and only one of those two is safe to call
  speculatively.
- **`AimInit`** (`0x00412090`) does NOT clear everything. Four of the ten
  per-army arrays -- both runs' points and deadlines -- are left as `.bss`
  gives them. Nothing reads a point whose live flag is clear, so it does not
  matter; but "the init clears the state" is not true of this function.
- **An `orig_` macro for a callee that is already ours went in for the THIRD
  time this session**, and `checkseams` took it out each time. The reflex when
  writing a call into the image is to reach for `orig_`; the question to ask
  first is whether the callee has already been reconstructed. `MapCode`,
  `Type2ActionA` and `SoldierKindForWeapon` were all caught this way.

- **`0x00448D60` names itself `TrooperDropItem`** in both its log lines, which
  is what identified `EvtDropItem` (`0x0041FC80`) above it: the `dropitem`
  action. Its search starts at inventory slot 1, not 0, and `TrooperDropItem`
  refuses slot 0 and slot 6 independently -- so the weapon a trooper has in
  hand is not what this drops. Its zero-point test is on the LOW SIXTEEN BITS
  of the packed point, so x==0 with a non-zero y still counts as "no point
  given" and is replaced; x==0 is the map's left edge and is reachable.
- **`SaveScriptName` and `LoadScriptName` are asymmetric on purpose.** The
  writer emits one of exactly two tags; the reader compares against the "no
  name" one and takes anything else as the name tag without checking, then
  reads the next dword as a length with nothing bounding it against a
  0x100-byte stack buffer. A file this game wrote cannot overrun it. The
  loader trusts the writer, and that is the shape of every save helper here.
- The binding on load is `0x0043F910`, which lower-cases the name and, when it
  is already taken, makes `name_1`, `name_2` off a `"%s_%d"`. So loading a
  save into a session that already holds these names does not collide -- it
  duplicates.

- **`RebuildTileCover`** (`0x0042BE10`) scans an interior margin of ONE and
  feeds every weighted tile to `TileCoverAdd`, whose own margin is TWO -- so
  the outermost scanned ring is handed to a function that rejects every tile
  in it. The two bounds were written independently and the stricter wins.
  Worth knowing before reading that loop as "every tile that can be covered".
- **`SetObjField530`** (`0x0043CD40`) decides whether a state change may
  interrupt the current animation with TWO tests that are not symmetric:
  leaving a state in 3..6 forbids it, entering one in 5..6 allows it, and the
  second overrides the first. So `4 -> 5` interrupts and `4 -> 3` does not,
  though both are inside the same band. Collapsing them into one condition
  loses the override.

  When the gate refuses, NOTHING happens -- including the field write. A
  caller cannot assume the state changed.

- **`JitterFacing`'s "same direction bucket" test is VACUOUS for every object
  whose record kind is not 3**, and the offline harness is what settled it
  rather than an argument about undefined behaviour. The bucket width is 3 for
  kind 3 and `0x20` otherwise; `RoundTo8` masks its second argument to a byte
  and then shifts by `7 - b` and `8 - b`, which x86 masks to five bits, so
  `b = 0x20` gives `1 << 7` and `>> 8` of a byte -- zero for every input. Both
  sides of the comparison are 0, they always agree, and the wobble is always
  kept.

  `tests/vectors.h` already carries `RoundTo8` vectors with `bits` of `0x2A40`,
  `0x7FFFFFFF` and `0xFFFFFFFE`, every one on that same masked-shift path, and
  `make selftest` passes all 6,852 against the original. **Where a
  reconstruction leans on behaviour the C standard does not define, the
  question is what this target does -- and there is a harness that measures
  it.**
- **`AddLevelRecord` and `AddNameRecord`** (`0x0043E160`, `0x0043E9A0`) are the
  same function over two record sizes, and are written out separately rather
  than shared: the original has two functions, and a helper would be a third
  thing that is not in the binary. The duplication is the comparison -- if the
  two ever stop matching line for line, one has been misread. Both allocate
  twelve records first and then grow by SIX, which are separate constants
  rather than one expressed twice, and neither checks either allocation.

- **A merged `functions.tsv` entry was met head on rather than counted.**
  `HeldWeaponObj` (`0x00459FE0`) and `ObjToAI` (`0x0045A030`) share one
  144-byte entry, so patching either alone would have marked both
  reconstructed -- the inflation CLAUDE.md warns about. Both are written.
- **`checkseams` caught the FOURTH spelling of a seam**, the one that looks
  like nothing: `winproc.cpp` passed `ADDR_OBJ_TO_AI` to `ForEachArmyObject`
  as a cast function pointer. That was correct while the callback was the
  original's and became a lie the moment `ObjToAI` was reconstructed, with
  nothing on the line resembling a call. It goes in by name now.
- **`SetKindFrames`** (`0x0045B000`) writes the kind INSIDE its
  mid-animation guard, not before it. A call arriving while row 0 is
  mid-animation changes nothing at all -- and yet row 1 is still considered on
  its own test, so the two rows can disagree about which kind they show. Row 1
  also takes a LITERAL frame rather than the table lookup row 0 gets.
- **`ObjToAI`'s two effects are independently gated**, so a dead Sarge gets
  the stance and not the fire clear -- the arm a single combined `if` would
  lose.
- **`combat` went out-of-phase twice running and came back on the third.**
  177,112 then 177,111 then 706, all on one build with identical logs. The
  three out-of-phase figures differ by a pixel or two rather than repeating
  exactly, which is what says "scene" rather than "defect" -- a repeated
  EXACT count is the signal to worry about.

- **`0x00417890` is the "Duck and cover!" cheat's effect**, named from the
  string its caller prints one instruction earlier -- two hundred `SpawnAt`
  calls scattered over a 620x480 span from the VIEW origin, so the barrage
  lands where the player is looking rather than somewhere in the level. **The
  order of its four `rand()` calls is part of the function**: they draw from
  the image's own LCG, and C does not sequence argument evaluation, so writing
  them inline as arguments would be free for the compiler to reorder. They are
  four locals in the original's order.
- **`SpriteKeyForKind`'s jump table has EIGHT slots and SIX distinct targets**
  -- selectors 0, 1 and 2 share one arm. Counting the bodies gives six arms
  where the switch has eight cases. Third instance of the same trap, after the
  state-2 sub-state table and `WeaponClassOf`.
- **`TileRegionOrBorrow`'s ring table is DOUBLED and its loop stops on a VALUE,
  not an index.** The eight deltas are followed by a copy of themselves, so the
  walk runs forward from any start for eight steps with no wrap test and
  terminates when the delta equals the one it began with. That is why there is
  no counter: the table's layout is the bound. Eight deltas that were not
  distinct would stop early; a table that was not doubled would run off the end.

  It also bounds-checks the RESULTING tile index rather than a margin, so a
  tile on a row's left edge can borrow its region from the right edge of the
  row above -- unlike the cover functions, which refuse a margin instead.

### An A/B needs a quiet machine, and the way to tell is the PARENT commit

This batch could not be verified on `bootcamp` or `combat`, and that is worth
recording rather than papering over. Under an external load average of 18-21
-- other users' processes, none of them ours -- the two halves of a run are
starved unequally: frame counts collapsed from the usual 6,000-13,000 to
1,700 and then to 64, `combat` went out-of-phase five times running, and
`bootcamp`, which has read **22 pixels on every run all session**, read
291,505 with an IDENTICAL log.

Re-running proves nothing when the distribution itself has moved. What does
prove something is stashing the batch and running the PARENT commit: it
failed the same way, with log differences and 306,172 pixels, on code that
had been clean hours earlier. So the noise is the machine.

`campaign` (2 pixels, identical log, 35 widget nodes byte-identical) and
`mission` (299, identical log, 16 nodes identical) still produced a coherent
signal and both passed. **Re-run `bootcamp` and `combat` on this batch when
the machine is quiet.**

- **`ShutdownSubsystems`' teardown table is now entirely named functions.**
  The last bare literal, `0x00445F40`, is `FreeSpriteRegistry`: close the open
  sprite file, force every sprite's refcount down to 1 so the release that
  follows is the last one, free the slot table and the id/slot pairs, zero the
  count and capacity. **The clamp is the whole reason it is not a loop of
  plain releases** -- `ReleaseSprite` frees at zero and takes one reference at
  a time, so a shared sprite would survive a single release and leak.

  It had a placeholder name, `ADDR_FREE_445F40`, from when only its caller had
  been read, and the alias ratchet is what stopped a second name landing
  beside it. Fourth time that check has caught its own author.

### An exact repeat can also mean both runs failed the same way

CLAUDE.md says a repeated EXACT pixel count is a defect signal where a
repeated approximate one is the scene. That held, and it needs one
qualification: `bootcamp` read **291,505 twice, on two different builds**,
which is as exact as a repeat gets -- and the control settled it the other
way. Checking out the batch that read 22 pixels this morning and running it
now gave **305,747**, with the script-load lines present on ONE side only.

So the pair being compared is a fully-loaded briefing against a
partially-loaded one, and that is a reproducible pair of images, hence a
reproducible count. **An exact repeat means the two runs failed identically,
which is a defect when they were otherwise comparable and starvation when
they were not.** The control tells them apart; nothing else does.

`quit` is the configuration that matters for this batch and it came back
**0 of 786,432** with an identical eight-message log -- `FreeSpriteRegistry`
is in the shutdown table, so that run executes it.

### The in-process check is the one the machine cannot spoil

`AM2_SELFCHECK=1` runs our function and the original **side by side in one
process**, so it does not care what else is on the machine -- which makes it
the right tool while an external load average of 14-21 leaves the whole-game
A/B unusable. It was 48 functions and is 49 now.

`SpriteKeyForKind` was added to it for its JUMP TABLE and for nothing else:
eight slots, six distinct targets, and arms laid out in a different order from
the one the table dispatches. `pick`'s edge set opens 0, 1, 0xFFFFFFFF, 2, so
it reaches every arm and both sides of the unsigned bound within a few calls.

**Proved in both directions, which is the only way this is worth anything.**
Correct: 6,272 calls across 49 functions, 0 disagree. With the arms read in
layout order -- the mistake a careful reader actually makes -- 3 disagree, and
the log names the function and the arguments:
`SpriteKeyForKind(0,1) -> 01000100, original 01300100`, which is set 0x20
where the original uses 0x26.

**What it cannot take.** A function that dereferences an argument twice faults
on scratch bytes, so `JitterFacing`, `RowAnimField4` and `ObjOverlayY` stay
out; one that indexes a table the game has not built yet takes the process
down, as `LookupOwnerObj` did. Scalars in, scalar out, and no second
dereference.

**There are no pure leaves left below the CRT line.** A scan of every
outstanding entry for one that neither calls nor touches a global returns
ZERO, so `tools/vectors.py` cannot be extended to any of them -- the offline
harness has finished its job on this half of the image, and the in-process
check and the A/B are what remain.

- **The offset ratchet SUPPLIED a meaning rather than preventing a
  duplicate.** `ResetType2Fields` (`0x004572A0`) went in with eleven
  `OBJ_OFF_FIELD_<hex>` names for the block it clears, and `checkoffsets`
  refused six of them: the four script fields, `OBJ_OFF_FOLLOW_UID` and the
  `OBJ_OFF_HIT_DIR` / `OBJ_OFF_HIT_TIME` pair were named long ago. Taking the
  existing names turned eleven unknowns into a plain reading -- drop the
  script binding, the weapon type record, the follow target and the last-hit
  record, wipe the tail block, re-stamp the facing.

  **"Grep for the offset before naming it" is usually stated as a way to avoid
  a second name. This is the other direction, and it is the more valuable
  one.** `OBJ_OFF_HIT_DIR` also corroborated the byte width the instruction
  showed, from the writing side.
- **`SelectIfOwn`** (`0x00458380`) checks CONTROL LAST, after every test that
  can refuse -- so a click on someone else's unit with CONTROL held leaves the
  selection alone rather than clearing it. Its health test is `!= 0` where
  `ObjToAI`'s is `> 0`, so a unit at negative health is selectable here and
  dead there. Two functions in one file reading one field two ways.
- **`0x004248A0` is deliberately left original.** It is MSVC static-init glue:
  five tail-jumping thiscall stubs for one global object at `0x00511A68` --
  a constructor, its member-constructor trampoline, an `atexit` registration
  and the matching destructor pair. Compiler-generated, and reproducing the
  `ecx` convention and the tail jumps would be work with nothing behind it.

- **`ResetObjOnCof` (`0x00457220`) cannot run on this install, and the reason
  is a missing FILE rather than a patched branch.** `ADDR_STATE2_ENTER` does
  `_findfirst("default.cof")` on entering a level and sets `0x00511DDC` only
  when it is found; the GOG install ships no `.cof` at all, so the flag is 0
  for the whole of every run and this function returns at its first
  instruction. Reconstructed because it is game code below the CRT line, and
  **verified by reading with no A/B able to say otherwise** -- stated plainly,
  because a clean suite would otherwise imply coverage that does not exist.

  Its null check comes AFTER five stores through the pointer, so it is dead;
  reproduced as written, because the order is the evidence.

  **The probe exists and was not taken.** Creating an empty `default.cof`
  would set the flag and turn reading into measurement -- but `0x00457070`
  does not merely test for that file, it opens and parses it, so an empty one
  feeds a parser nothing and the outcome is unknown, and it means writing into
  the shipped game directory. Worth doing on a throwaway copy of the install;
  not on this one.

- **`StartShake`** (`0x0042B2E0`) is the screen shake, and its four
  comparisons are NESTED rather than independent. Each `jle` skips everything
  after it, so a request whose time is not greater than the current one never
  reaches the step tests at all. Reading it as four separate maxima is wrong
  for every case but the strongest.

  The two phases are cleared UNCONDITIONALLY, above the first branch, so even
  a request that changes nothing else restarts the oscillation. The two steps
  are compared by ABSOLUTE value and the time and amplitude are not -- a step
  is signed, so "stronger" means further from zero. Its four presets are
  `{250,25,12,2}`, `{500,16,35,2}` and `{750,45,17,2}` plus an all-zero one:
  the amplitude is 2 for every one that does anything, so they differ in
  duration and in which axis dominates, not in how far the screen moves.
- **I nearly shipped it never installed.** The scripted edit that adds the
  `patch_replace` line silently matched nothing -- `mapdraw_install` has no
  `int rc = 0;` -- and the build was clean, `make check` passed, and the
  function was dead. This is the exact failure CLAUDE.md records for
  `ApproxDistXY`, `AngleDelta`, `RoundTo8` and `WriteSaveTag`, arrived at
  from the other direction: not a `return` above the call, but an insertion
  that never landed.

  What caught it was grepping for the install line, and the cheap standing
  check is the coverage count -- it does not move for a function that was
  never patched. **Measure the count after every batch; a build that compiles
  proves nothing about whether the patch is in.**

- **`SelectRankedWeapon`** (`0x00406AB0`) is `SelectBestWeapon`'s twin and
  differs in two things that matter. It scores with `MapCode18To28`, whose
  five live codes rank in an order that is a lookup rather than a formula, so
  the two functions can disagree about which weapon to hold. And it writes the
  slot ONLY when the winner is not slot 0 -- a unit whose best weapon is
  already the first keeps whatever selection it had -- where its twin always
  writes and then applies a soldier kind as well.
- **I reconstructed its scorer a second time, and `checkpatches` refused the
  build on BOTH counts at once** -- "0x00406A40 patched 2 times" and a
  22nd `ADDR_` alias. `MapCode18To28` has been in `misc.cpp` all along.
  Fourth near-miss of this kind after `ScriptCompare`, `AllocUid` and
  `SwapColourBytes`, and the first where one tool caught the duplicate patch
  and the duplicate name together.

  **Grep the tree for the ADDRESS as well as for the name.** The name would
  never have collided: I called it `WeaponRank` and the existing one is
  `MapCode18To28`, which is exactly why the address is the thing to check.

  Its caller count was also wrong in the old comment -- one, where the image
  has two -- and fixing it was free once the address was the question.

### Rank the queue by what is ALREADY NAMED, not by size

Size ranking has been the picker for twenty batches and it stopped paying:
the small entries left are the ones whose types are not established, so each
costs three to six new names on thin evidence. A better order is how many
addresses a candidate touches that `orig.h` does NOT already know.

Measured: **eighteen outstanding entries reference nothing unnamed at all**,
from 112 bytes up to 272. Those are pure transcription -- the reading is
already done, by whoever named the callees. The scratch tool that ranks them
disassembles each entry, collects every call target and every absolute
operand in `0x400000..0x700000`, and subtracts the addresses `orig.h` names.

- **`ObjRowsMaskAt`** (`0x00435440`) tests the FIRST row only, and `orig.h`
  said it "walks the object's OBJ_OFF_ROWS and tests each row's sprite".
  There is one load of `rows`, one `[rows + ROW_OFF_SPRITE]`, and no loop;
  the row count is read only to refuse an object that has none. The
  description came from the name and the name came from a call site --
  **third time reconstructing a function is what corrects its own macro's
  comment.**

  Its unrecognised-format arm answers **1, a hit**, where all four early
  exits answer 0. Format 0 means the image is a DirectDraw surface, so there
  is no software mask to read and "assume solid" is the only answer available
  without a lock.
- **`RemapInventoryUids`** (`0x004276F0`) is the savegame uid fixup. It
  clears its gate flag BEFORE testing the type, so an object of any other
  type still loses it -- the flag means "seen by this pass", not "remapped".
  A uid with no table entry is left alone rather than zeroed, so a save
  referring to something gone keeps a dangling uid.

- **`MsgListInsert`** (`0x00401150`) sorts on an UNSIGNED key -- `jbe` and
  `ja` -- so a key with the top bit set sorts above everything rather than
  below, which is what a wrapping counter wants and not what a signed reading
  would give. It is a stable insert: ties go after the nodes already there.

  It also has a DEAD EARLY RETURN that would have skipped the count. After
  taking `next` and finding it non-null the original reloads and re-tests it,
  and the impossible zero falls into a path that releases the mutex and
  returns without incrementing `MSGLIST_OFF_COUNT`. The second test cannot
  fail, so the list can never hold an uncounted node. Not reproduced as a
  branch -- no input reaches it -- and recorded instead, since a reader
  comparing the two will find one `ret` fewer than the original has.
- **`EvtArmyAttach`** (`0x0041FDB0`) is the FIFTH non-advancing removal loop.
  Its filter is a VALUE rather than a predicate, `-1` meaning all, and the
  accessor it compares against is only consulted when the filter is set.
- **`quit`'s thread-line difference has now appeared on BOTH sides**, one run
  each. A defect would favour one side consistently; an extra
  "Receive thread got event 0" on the original in one run and on the
  reconstruction in another is the shutdown race and nothing else.
  **Which side a difference lands on, across runs, is evidence.**

- **`OpenSaveForLoad`** (`0x00425950`) exists for its REWIND. Both of its
  checks consume from the stream -- `CheckSaveTag` reads four bytes and
  `LoadGameProcSection` reads its whole 0x438-byte block -- so without the
  `fseek` the caller would resume in the middle. It rewinds to 0 rather than
  past the header, so the caller reads the gameproc section again for itself.

  It leaves the process in the SAVE DIRECTORY on every path after the chdir,
  including both failures; the caller owns that. Its two emptiness tests read
  one byte each rather than a length, and in the opposite order to the order
  the names are used.

  The `__LINE__` it hands `CheckSaveTag` is 1320, from `gameproc.cpp` as it
  stood in 1999. It is passed verbatim: it names a line in the original
  source, not in ours.
- **A fourth `orig_` macro for an already-reconstructed callee**, caught by
  `checkseams` again. The reflex is now well enough attested to invert: when
  writing a call into the image, **assume the callee is already ours and go
  looking for the proof**, rather than the other way round.

### The offset ratchet was blind to FLAGS, and I walked into the gap

`tools/checkoffsets.py` watched `*_OFF_*` families and nothing else. Two
batches ago `OBJ_FLAG_REMAP_DONE` went on `0x400` beside `OBJ_FLAG_SELECTED`
-- one bit, two names, two readings -- and every check passed.
`ToggleSelect` is what exposed it: that flag is what selection SETS.

So the correct reading of `RemapInventoryUids` is better than the one it
shipped with. It does not clear a bookkeeping bit; **it deselects every type
2 it remaps**, which is obviously right for an object that has just come off
disk.

The regex now covers `_OFF_` and `_FLAG_`, with the family captured so
`OBJ_OFF_` and `OBJ_FLAG_` stay separate -- an offset and a bitmask sharing a
number means nothing. Tested in the failing direction by putting the
duplicate back: 15 against a baseline of 14, named and refused.

Widening it found one PRE-EXISTING alias, which is why the baseline is 14 and
not 13: `OBJ_FLAG_OVERDUE` and `OBJ_FLAG_REPLACED` are both `0x2`. Left as
backlog rather than guessed at.

- **`ToggleSelect`** (`0x00413710`) cannot deselect the LAST object -- the
  removal path is gated on the selection holding more than one, and the guard
  is explicit rather than an accident of the loop. Removal also DETACHES the
  object where addition attaches nothing, so the two directions are not
  symmetric. Its CONTROL test comes FIRST, where `SelectIfOwn`'s comes last.
- **`SetObjContext`** (`0x00457A60`) reaches Sarge two ways -- directly, and
  as the first listed object of a type 3 -- so selecting the vehicle Sarge is
  riding gives the same pointer as selecting Sarge. Any type that is neither
  2 nor 3 leaves the pointer mode ALONE, having already written the context
  globals.

- **`WalkCellAtPoint`** (`0x0042A110`) is the third member of the
  point-query family, and its own macro called it "a CALLBACK instead of a
  chain". It is BOTH: the callback is a FILTER whose answer decides whether
  the object joins the chain, and the chain is built and returned exactly as
  `ObjectsHitByPoint`'s is -- the macro even declared the return type `void`.
  Fourth time reconstructing a function is what corrects its own comment.

  The predicate sits BETWEEN the two hit tests, not before or after both. The
  mask test is the per-pixel one, so the filter gets a chance to reject before
  it runs but only after the cheap rectangle has accepted. Moving it either
  way changes how often each runs and what a predicate with side effects sees.

### Grep the ADDRESS first -- and now the picker does it for you

Five batches running, I wrote a function, invented an `ADDR_` name, and had
`checkpatches` refuse the build because the address had been named years ago:
`ObjToAI`, `FreeSpriteRegistry`, `MapCode18To28`, `ObjRowsMaskAt`, and now
`WalkCellAtPoint`. **The name never collides -- only the address does**,
which is exactly why "grep for the name" never catches it.

The ratchet caught all five, so nothing shipped wrong. But it caught them
after the work, and twice the established name carried a WRONG description
that only the reconstruction could correct. So the scratch picker now prints
what `orig.h` already calls each candidate, beside the size and the unknown
count. A rule that has been ignored five times should be turned into a tool.

- **`FreeAaiTables`** (`0x00434B60`) gates its first block on the ARRAY and
  its third on the COUNT, which is not symmetry. A record-list array with a
  zero count is freed correctly -- the loop just does not run -- while a NULL
  AAI array with a non-zero count would be indexed. Nothing sets one without
  the other; the original takes the risk either way round, and it is
  reproduced.

  Its two inner frees are DIFFERENT functions -- `FreeRecordList` is a real
  teardown, `FreeIfNotNull` the one-line guard its name says -- which is the
  only thing in the function that says the two tables hold different kinds of
  thing.
- The picker paid for itself immediately: `checkseams` caught two more
  `orig_` macros here (fifth and sixth this session), and both callees were
  already ours. That is the same failure the address-first rule addresses,
  one level down -- **the CALLEE's address is worth grepping too**, not only
  the entry's.
- **`quit`'s thread-line race has now been seen three times, twice on the
  original side and once on the reconstruction's.** Well enough characterised
  to stop re-running for it.

- **`AmmChecksum`** (`0x0042C350`) does not parse IFF, it reads ONE fixed
  layout: `FORM`, a length, `MAP `, then a chunk tag that must be `CSUM` with
  a length of exactly 4. There is no loop, so a `.amm` whose first chunk is
  anything else answers 0 -- and 0 is also what a valid zero checksum answers,
  which the function cannot distinguish for its caller.

  **THE BODY SAYS ONE ARGUMENT AND THE CALL SITE SAYS TWO.** It reads only the
  map name, and does its chdir with the folder global's ADDRESS as a literal
  rather than with the parameter it was handed -- the same pointer either way,
  which is why nothing has ever gone wrong. The existing `orig_` macro had the
  arity right and my first reading of the body had it wrong.

  **A body cannot tell you its own arity when it ignores an argument.** Read
  the call site for that, the way `RandomPointAhead`'s unused first parameter
  was settled.

- **`ObjMatchesSel`** (`0x00437400`) is two functions behind one entry, and
  its FIRST argument decides which -- the other two change meaning with it.
  Non-zero selects by NAME, and the second argument becomes an index into the
  script name table whose entry must be a `REF` -- a name used before it was
  declared -- whose value is this object's uid. Zero selects by MASK, and the
  second argument becomes nine independent tests.

  **The army bits are four separate tests, not a field.** A mask asking for
  army 1 and army 2 can never match, rather than matching either; the same
  holds for the type bits, where 0x08 and 0x20 together want an object that is
  both a type 2 and a type 3.

  Its last test is INVERTED in the original -- every other bit branches to the
  failure on a mismatch, and the army 3 test branches to SUCCESS on a match
  and falls through. Same meaning, one instruction shorter, and it is why
  `eax` is set to 1 in the middle of the function rather than at the end.

- **Three target predicates in one `functions.tsv` entry** -- `ObjIsOurs`
  (`0x00403600`), `ObjIsLiveTarget` (`0x00403660`) and `ObjIsHittable`
  (`0x004036C0`), at 0x60, 0x60 and 0x30 bytes. Patching any one would have
  marked all three done, so all three are written. Second merged entry taken
  whole this session.

  All three open on `OBJ_FLAG_BIT8` and answer yes without looking at health,
  the destroyed flag or the army -- an override whose setter is not
  established, but which three independent functions agree about.
- **`ObjIsHittable` DISCARDS its last call's result.** It calls `ObjIsType4`,
  pops the argument, and falls into the same `mov eax, 1` the override arm
  jumps to, so the answer is 1 either way. It is a call for its side effect,
  and `ObjIsType4`'s only side effect is to LOG "uid wasn't a weapon!". So
  reaching that point with anything else is a complaint in the log and a yes
  to the caller. Reproduced with the result discarded, because removing the
  call would remove a log line -- one of the few things an A/B can see.
- **A fourth reading of the health field.** `SelectIfOwn` takes `!= 0`,
  `ObjToAI` takes `> 0`, and `ObjIsLiveTarget` takes BOTH -- zero refused for
  everything, negative refused only for ITEMS, so a type 2 at negative health
  is still a live target. Four functions, three readings, all reproduced.
- **`ObjIsOurs`'s multiplayer guard is a flat refusal that runs FIRST**: in a
  session, a type 2 of soldier kind 7 is never ours even when the armies
  match. Outside a session the guard does not run and the same unit is ours
  like any other.

- **`BuildRowsFromDef`** (`0x00434C90`) is `BuildRowSet`'s sibling one entry
  earlier, and the two differ in where their input comes from and in their
  SPEC SIZE: twelve bytes here against sixteen there. Anything that assumes
  one stride from the other is wrong by a third. It also STORES the def in the
  header's first dword where `BuildRowSet` zeroes it, which is the only way to
  tell afterwards which built a given set.

  Its per-row flag depends on the OBJECT, not the spec: bit 0 is set only when
  the object's x is non-zero AND it is not destroyed. An x of exactly zero is
  the map's left edge and reachable, so that is a live distinction rather than
  a null check -- the original tests the same argument it adds to every row's
  x. Fourth writer of `ROW_OFF_FIELD_26`, and consistent with a depth key.

### A widget diff whose CONTENT said it was the drive

`campaign` failed the widget oracle -- 32 nodes against 35, which is the
exact comparison with no budget. Reading the diff settled it in seconds and a
re-run only confirmed it: one side showed the six-button menu and the other a
scrollable list with a scrollbar and two arrows. **Different SCREENS, not a
different widget layer.** A defect in the widget code perturbs fields within
a matching tree; a drive that is a screen behind replaces the tree wholesale.

The re-run was identical at 35 nodes. Worth knowing that this oracle has a
timing failure mode too, and that its own output distinguishes the two.

- **`WeaponFrameReady`** (`0x004499A0`) has THREE answers, not two: 1 means
  "this weapon kind has no frame rule"; 0 means "the rule exists and this
  frame fails it"; anything else is `Field51MeetsMin`'s answer on the row.
  A caller reading it as a boolean gets the right shape and loses why.

  Its dense switch covers kinds 1..0x2B and sends 27 of them to the default,
  so the rules are for kinds 1, 2, 4, 5, 7..12, 29, 30 and 43 only. **Two of
  its arms test the SAME frame by different routes** and the compiler did not
  merge them; written as one case each way round, from the table rather than
  the code layout.

  Arm 0 has a guard the others do not -- a positive `OBJ_OFF_FIELD_44` answers
  1 before the frame is looked at.
- **A second "call whose result is discarded"**, after `ObjIsHittable`'s
  `ObjIsType4`: this one calls `ClassifyByCode74` and overwrites the answer
  with the row's frame on the next instruction. Reproduced, for the same
  reason -- it is there for what it does, not what it returns.

- **`ConsiderSighting`** (`0x004074A0`) fills its output record BEFORE the
  ownership test and reveals AFTER it, so a caller always gets the sighting's
  numbers when the geometry passes and the two-second reveal happens only when
  the observer is ours or an ally. Splitting those two apart is the obvious
  tidy-up and would change what the caller sees.

  Its cone is `|AngleDelta| <= 3` on a byte -- about eight degrees either
  side, which is why a unit does not reveal everything around it.
- **A SECOND READER OF `OBJ_OFF_FIELD_530`, AND IT DISAGREES WITH THE FIRST.**
  `SetObjField530` treats that field as a small state, 0..6, indexing a table
  of animation frames; this reads its low byte as an 8-BIT BEARING and hands
  it to `AngleDelta`. Both cannot describe the same quantity unless the states
  double as headings. Recorded rather than resolved -- the field is named for
  its offset precisely because of this, the way `OBJ_OFF_CHAIN_UID`'s two
  readings are.
- **The control was used rather than a fourth re-roll.** `combat` came back
  out-of-phase twice for this batch (177,231 then 175,848, identical logs).
  Stashing and running the PARENT -- clean at 814 pixels earlier the same day
  -- gave **306,178 with log differences**, worse than either. So the machine
  cannot run that configuration at present and this batch is not the cause.
  `mission` is clean at 290.

- **`ConsiderSightingB`** (`0x00408580`) is the previous batch's sibling, and
  the differences settle a layout question rather than raising one. **+0x3C is
  a maximum RANGE in one and an ENABLE in the other**, and both readings are
  literally what the instructions do -- so the two records differ rather than
  one being misread. The three fields they share (observer, range, bearing)
  agree exactly.

  Its cone is EIGHT and compared `>=` where the other's is three compared `>`
  -- wider, and off by one relative to it.

  **The bearing it compares is the OUT record's, not the seen object's.** The
  other reads `OBJ_OFF_FIELD_530` off the object; this reads a byte the out
  record carries and, in its tail, writes the record's bearing back there. So
  that record accumulates a bearing across calls and this function is a step
  in a sequence rather than a standalone test -- which is also why it has a
  tail that runs on EVERY path, including the ones that refuse.

- **`InitObjFromAai`** (`0x00433880`) builds an object out of an AAI record,
  and **every field name on that record comes from where it lands**: +0x2C is
  copied to `OBJ_OFF_MAX_HEALTH` and `OBJ_OFF_HEALTH` in the same breath, so
  it is the starting health and the maximum at once; +0x2E to
  `OBJ_OFF_HEIGHT_ADJ`, +0x2F to `OBJ_OFF_RANK`. That is the strongest naming
  evidence available for a record carrying no strings of its own.

  Its flags are OR'd from two sources and nothing is cleared, so a caller
  cannot use it to turn a flag off. Its position is one packed dword read
  twice -- whole for `ObjInitCommon`, and as two halves for
  `BuildRowsFromDef`, the high one fetched by reading two bytes further up the
  stack.

  **Nine arguments and the ninth is never read.** Both call sites push nine
  and `add esp, 0x24` confirms it; every stack read in the body lands on one
  of the first eight. Third unused parameter in this tree after
  `RandomPointAhead`'s first and `AmmChecksum`'s second.

- **`SetObjTablePair`** (`0x0044BA70`) writes TWO pointers into the 256-byte
  record table and indexes them DIFFERENTLY: `+0x52C` by the kind it was
  given, `+0x534` by the object's army's comm slot. One is what the caller
  asked for and the other is who owns it; reading them as a pair of the same
  thing loses that. Only the kind pointer is propagated through the
  sub-record, so whatever reads the propagated copy never sees the owner's.

  **It confirms another function's comment from the other side.**
  `ADDR_SCRIPT_SET_OBJ_TABLE` is documented as writing "+0x4C0 and +0x4C8 of
  the sub-record at obj+0x6C" -- and `obj + 0x6C + 0x4C0` is exactly `+0x52C`.
  Two routes to one pair of fields, arrived at independently and agreeing.

  Its army lookup goes through the UID rather than the object it just
  resolved from it: the same value by a longer route, reproduced because the
  two would differ if a uid ever resolved to an object of another army.
- **`0x00438F10` is left for now and the reason is its RECORD, not its code.**
  Its sibling `ObjBoxAction` is done; this one reaches a sprite through
  `*(rec + 0x0C)` and a box at `+0x20..+0x2C`, and no structure in `orig.h`
  has that shape. Its caller disassembles as garbage before the call site, so
  the type is not settled from there either. It is the last sub-128-byte
  entry outstanding, and it is outstanding for want of a NAME rather than for
  want of reading.

- **`ConsiderSightingC`** (`0x00404F40`) completes the family, and its
  maximum-range field carries a MAGIC VALUE that cuts both ways. When the
  maximum is exactly `0x1000`, a bearing outside the cone is FORGIVEN and the
  reveal is SUPPRESSED -- one constant doing two opposite-seeming jobs: it
  sees in every direction and tells nobody, which is what a passive sensor
  looks like. Writing the two tests as separate ideas would hide that they are
  the same number.

  It has a second tail the other two lack, and that tail runs on EVERY path
  including the ones that never looked at the geometry -- a record of kind 3
  whose range is in bounds bumps the out record's state from 2 to 3, and the
  range is re-tested there rather than reusing the earlier answer, so the bump
  can happen on a call whose cone test refused.

  Its last tail writes the SEEN OBJECT and not just the out record:
  `OBJ_OFF_FIELD_578` goes to 1 on any recorded hit. That makes it the only
  one of the three reaching back into the object it was asked about for a
  reason other than the reveal.

  A third record layout, and the offsets NEARLY line up with the first without
  doing so: observer, range and bearing shift by exactly four, while the
  enables and the maximum do not. So the three are related and distinct rather
  than one record with a longer header.

- **`CommPlayerLeft`** (`0x0040F790`) releases three pause reasons per slot,
  and they ARE `0x800 << slot`, `0x10 << slot` and `0x20000 << slot` -- but
  **the original does not compute them**. All twelve are literals across four
  `cmp slot, N` arms. Reproduced that way: collapsing them into a shift is a
  claim the binary does not make, and would invent behaviour for a fifth slot
  where the original has none. A slot outside 0..3 releases NOTHING and still
  marks its army ready.

  Its last field is written through the GLOBAL comm object rather than through
  `this`, where every other access in the function goes via the pointer. The
  same object in practice; reproduced because it is the only line that would
  still work if `this` were something else.
- **It was verified on `mpoptions`, which is the strongest evidence this
  project has for a comm function**: state identical (5 lines), widgets
  identical (131 nodes), log identical (35 messages), and four screenshots
  inside budget. `multi` and `campaign` clean too.

- **`TroopSubParse`** (`0x0044BEA0`) reads a BIG-ENDIAN header byte by byte --
  four loads shifted 24, 16, 8, 0 on a little-endian machine, so it is a wire
  order and not a struct read. Its low 29 bits are the uid and the top three
  are one presence flag each, and **the army is then shifted into the very
  bits the flags came out of**: the key is
  `(word & 0x1FFFFFFF) | (army << 29)`. The uid on the wire carries no army;
  the receiver supplies its own, which is what lets the flags live up there.

  Its position is three bytes for two twelve-bit fields, and the original
  stashes two of them in its OWN ARGUMENT SLOTS and reads them back -- which
  is why the reconstruction needs two locals where the disassembly appears to
  need none.

  Nothing checks the lookup: a stale uid faults the receiver. And the final
  fire-mode test reads a field this function may never have written, so a
  record carrying neither optional field still clears the F588/F58C pair.
- **The uninstalled-patch failure recurred, and the check I added for it
  caught it.** The scripted `patch_replace` insertion matched nothing again --
  `commmsg_install` has no `int rc = 0;` -- so the function was written, built
  clean, passed `make check`, and passed a full A/B **while not being
  installed at all**. The coverage count did not move and
  `0x44BEA0 in reconstructed()` read False. Second occurrence after
  `StartShake`; **the count is the check, and it must be read before the A/B,
  not after** -- that clean A/B was of the parent's code.

### The whole suite, on a quiet machine, clean

The external load fell away and `tools/ab.sh all` ran end to end: **all
seventeen configurations clean** -- `bootcamp`, `mission`, `combat`,
`campaign`, `controls`, `difficulty`, `audiovol`, `menuscreens`, `movies`,
`multi`, `mpoptions`, `df`, `state3`, `quit`, `intro`, `windowed`, `audio`.

That clears the verification debt this file recorded from batch fifteen, when
`bootcamp` and `combat` could not be compared at all. `combat` came back
in-phase at 730, `bootcamp` at its usual figure, and `mpoptions` at **0**
pixels with its 131-node widget tree and 5-line state dump identical.

Worth noting for pacing: none of the thirty-odd batches committed under load
turned out to be hiding anything. The control technique -- run the parent
commit -- gave the right answer every time it was used.

- **`PickWeaponSlot`** (`0x00406800`) returns TWO answers and a caller must
  read both: the slot goes to an out-parameter and the permission is the
  return. The out-parameter carries `-1` for "this kind needs no slot" and
  `-2` for "all six full", so only a non-negative value is a slot.

  **It stops at the first slot that DECIDES, not the first free one.** Two of
  its three stopping conditions return a verdict rather than a slot to fill,
  which is why the return is not simply "found" -- a caller taking the index
  without the verdict would put a weapon into an occupied slot.

  An unresolvable uid is treated as an empty slot and REPAIRED in passing:
  the slot is zeroed and the index left pointing at it. It is the only place
  that clears one.

- **`TryTakeWeapon`** (`0x00406720`) answers a VALUE, not a boolean: every
  success returns the candidate's thing code and every refusal returns 0, so a
  thing whose code is 0 is indistinguishable from a refusal. That is also why
  the code is computed BEFORE the drop rather than after -- the return has to
  survive the whole tail.

  **Its full-inventory walk seeds its "best" with the CANDIDATE's code, not
  slot 0's**, and starts at slot 1. So a weapon worth less than everything
  already carried finds no victim, the slot stays -2, and the function
  refuses. That is the entire mechanism by which a unit declines to swap down,
  and it is invisible unless the seed is read carefully.

  Its one non-slot refusal is the ammo test: an occupied slot blocks the take
  only when the weapon already there is FULL -- `ITEM_OFF_AMMO` at least the
  candidate type's `ITEMTYPE_OFF_CAPACITY`. "Already carrying one" is not a
  refusal by itself.

  It also reuses its own argument slot as `PickWeaponSlot`'s out-parameter --
  `lea eax, [esp+0xC]` points at where `cand` was pushed -- which is harmless
  and is why this needs a local where the disassembly appears not to.

- **`SealMapEdges`** (`0x0042BCF0`) computes its five-tile border margin from
  the map's HEIGHT ON BOTH AXES -- the band is `5 <= y <= height - 5` and
  `5 <= x <= height - 5`, the same `height - 5`, not `width - 5`. On a square
  map the two agree and nothing shows; on a wider one a column near the right
  edge goes unflagged. It is one register the original computes once and
  compares twice, so this is what the binary does rather than a transcription
  slip. Reproduced.

  **Its third pass reads a weight it may itself have just written.** A tile
  marked `AM2_TILE_OPEN` is given a full cell weight and then, two
  instructions later, has `AM2_TILE_BLOCKS` set because of it -- so "open"
  implies "blocks", within one iteration. That is the other half of the
  inverted polarity already recorded for bit 0: `BlockWeightChain` penalises a
  tile whose bit 0 is CLEAR, and here the bit being SET is what makes the tile
  impassable.

  Bit 1 (`AM2_TILE_NEAR_EDGE`) had no name and this is its only writer.

## Stop condition

The loop's `completion_promise` is now **every game function below the CRT
line (0x0045C000) patched**. Measured: **1,037 of 1,239** entries in
`docs/functions.tsv` below that address have a patch inside them, from 1,190
patched addresses. That figure counts merged entries generously and is a
ceiling on progress rather than a floor -- read it with `tools/merges.py`.

With a target, the strategy changed: rank what is left by SIZE and take the
small ones in batches. Forty-one batches have gone in and the 202 entries outstanding start at 48
bytes.

**A placeholder name is fine until the thing has a real one.** `DefFinish`
went in naming its five callees `ADDR_DEF_STEP_*`; two commits later two of
them were reconstructed and `checkpatches` refused the second name. That is
the alias ratchet doing what it is for, not an argument against
placeholders.

**Four of the twelve are three-argument pass-throughs** -- a thunk that
forwards its arguments to one large function and adds nothing. The compiler
did not make those; a source-level wrapper is what compiles to one. Worth
knowing before reading one as a place where something happens.

**A batch's real cost is where the functions can LIVE.** `DrainMsgList`
belongs with the other five but had to go in `commmsg.cpp`, because
`gameproc.cpp` is in `SELFTEST_SRC` and a call from there to
`MsgListRemHead` drags eleven win32 symbols into the offline harness.

- **The HUD**, all seven classes: the top strip (paint and update), the edge
  strip (both), the radar (paint plus `RadarBlipColour`, `DrawBlip3`,
  `DrawBlipPulse`, `DrawBlipSquare` and `DrawRectFast`), the squad panel's
  paint and the sarge panel's update.
- **`DrawTextVertical`**, the drawing half of a pair whose measuring half was
  already ours.
- **Four more from the bracket batch**: `TextListPaint`, `CheckboxPaint`,
  `CountButtonPaint`, `MpNamePaint`.
- **`ItemIsReady` and `ItemTypeName`**, closing the seams the sarge update
  left; `checkseams` failed the build the moment they landed.

### Two real defects, and how each was found

**`HudSquadPaint` drew an INVERTED RECTANGLE.** The wide backdrop runs from the
portrait's right edge out to `x + 131`; the two ends were swapped, and crossed
ends make `IntersectRect` reject the rect so `ClearRegion` never ran. **An
inverted rect fails silently rather than drawing wrong**, which is why it
presented as missing output. Found by reading the differing PIXELS -- original
black, mine showing the map's green -- after two wrong guesses from the
disassembly. Three other fixes went in alongside and none of them was the bug.

**`MpNamePaint` KILLED THE PROCESS.** `MPNAME_OFF_TEXT` is a pointer and
`orig.h` says so in as many words; passing the address of the field to sprintf
formatted over `MPNAME_OFF_FLAG`, `_INK` and `_PAPER`. The signature was
distinctive: 28-38% of the frame, a ZERO-BYTE widget dump where the original
wrote 131 lines, and the checksum sequence run once instead of twice. That is
"the process is gone", not "wrong pixels".

### Two latent bugs found beside the first

`HudTopPaint` and `HudSargePaint` were passing `DrawSpriteClipped` the box they
started from rather than ClipRect's ADJUSTED pair. Both agreed with the
original only because their sprites are never partly outside the clip -- **when
nothing is clipped the two forms are identical**. `HudCommandsPaint` genuinely
uses its saved origin and says so. Both conventions exist in this binary and it
has to be read per function, which is what made one assumption reach three
places.

### Read the instruction between the accesses

Twice this batch a single instruction between two stack accesses decided what a
slot means. `DrawBlipPulse` interleaves `pop edi`/`pop esi` into the middle of
two arms, so the same `[esp+0x10]` names arg3 before them and arg4 after --
taken at face value the two colours come out swapped. `CountButtonPaint` has a
`push edi` between its two colour stores, so what looks like one slot written
twice is the ink and the fill; read at face value the default ink looks dead
and the ink looks read uninitialised. Both were written down wrongly before the
stack was recounted.

### Where the numbers stop

Six of the twenty have a real pixel oracle. Establishing that took two
corrections: the screenshots put the game's 640x480 frame at 1:1 in the TOP
LEFT rather than scaled into 1024x768, so two of three HUD rectangles being
sampled were black border; and a 0 means nothing until the region is known
non-blank.

Four functions have NO check at all and say so in their own source.
`DrawBlipPulse` and `DrawBlipSquare` are gated on an `OBJ_FLAG_BIT4` no drive
sets -- poking it runs them, but the radar's pixels cannot discriminate their
colour assignment: swapping the two colours scores 62 against the original
where the correct code scores 54. `TextListPaint` and `CheckboxPaint` are
unreached on every configuration here, and those zeros are REAL rather than
blind -- `blindspots.py` files both under "reached by address". Their
constructor chains are recorded so the next session can find the screens
instead of repeating the drives.

`ItemIsReady`'s 1-vs-2 return is unobservable: the sarge paint tests `> 0`.

### The ratchets, and the rule that keeps being ignored

`checkpatches`, `checkseams` and `checkoffsets` all fired at once on
`MpNamePaint` for one cause -- three "helpers" named without grepping, which
were already named AND already reconstructed. That is the rule quoted twice
earlier in the same session. Aliases went 21 -> 24 and are back to 21.

`checkclaims` failed the build on the bracket count at every single step, which
is the whole argument for it.

### Process failures worth not repeating

A second `ab.sh` was launched while the first was still driving, and then --
having just written that down -- `src/` was edited during the next one. Both
runs were invalid and were thrown away rather than read. `ab.sh` rebuilds on
every launch; there is no safe edit while it runs.

## Next

- **This file has grown past being readable and that is now an item.** Two
  sections hold 81% of it -- "The next target, read but not written" at ~2,290
  lines and "Leads" at ~2,790 -- while the four sections that answer "where is
  this" are about 5%. A heading in the singular covering two thousand lines is
  an archive, not a target. Triage is deliberately NOT being done as a side
  effect of a status update; it wants its own pass.

  A second `## Next` was found in there and has been renamed rather than
  merged: it was a status-of-the-front summary under a heading promising a
  queue, and folding it would have destroyed something useful on the strength
  of its title.

- **A mission with something in it to kill.** `ab.sh combat` reaches the
  damage path but nothing dies, so `FreeItem`, `RemoveFromItemList`,
  `DamageRoach` and `ObjDie` are still unexercised. Boot Camp's opening has no
  target that can be destroyed; this is the "different mission, not a longer
  one" ask, now narrowed to exactly what it needs.

- **AI movement**, separately: `MiddleRegionLink` and the region walk are only
  reached when something is *pathfound* rather than driven, so holding a key
  does not do it.

- **A drive that types a cheat -- half solved.** The console key is **binding
  index `0x13`, scancode `0x0E`, BACKSPACE**: `key 0x0e tap` in a live mission
  puts `0x004185C0` into `ADDR_CHAR_HANDLER`, verified by reading the global
  back. What still does not work is typing into it -- `type ...` posts the
  WM_CHARs and `\r` posts the `0x0D` the submit tests for, and
  `ScriptRunLine`'s counter stays at 0. The characters are dropped between the
  handler and the submit, and that is where the next attempt starts rather
  than at the key.

  Finding the key needed the bindings table, not a scancode search: the push
  is `0x13` and that is an *index* into `ADDR_KEY_BINDINGS`, whose twentieth
  pair is `0x0E`. Read as a scancode it gives R, which is wrong and plausible.

- **A drive that observes the mouse pick** is the older half of that. It would close `PickObjectsAt`, and it is close to the drive the
  combat path has been waiting for -- `DamageObject` is 0 on every
  configuration, which blocks `FreeItem`, `RemoveFromItemList`,
  `Type2ActionB`, `DamageRoach` and `PointActionA`.

  **Two proposals died here, and the second killed the first.**

  `OBJ_FLAG_SELECTED` is not the oracle. The leader's flags read `0x00000C01`
  the moment Boot Camp goes live -- bit 0, SELECTED and REVEALED all already
  set -- and a 40x40 sweep of clicks never moved them. (The sweep "found" a
  hit on its first click, which was the probe being wrong: it tested
  `flags & 0x400` instead of comparing against the value before the click.)

  Then the reason that did not matter: **`ObjectsHitByPoint` is not the mouse
  pick at all.** It went in under that name because three of its five callers
  sit in the HUD band -- naming a function from its call site, in a comment
  that cited the callers as evidence while doing it. Not one passes a cursor:
  two build the point from a world origin plus a table offset, one from
  `PointOfTile`, one from a float projection. Renamed.

  So the mouse pick is still unfound, and the "3,872 calls nothing watches"
  measurement stands on its own -- it is about a world-point query, not about
  clicking.
## A correction: two functions were not unexercised

**A hand-written probe clicked BOOT CAMP at the wrong Y and never entered a
mission**, and three findings in this file rested on it. `ab.sh` clicks
(306, 143); the probe used (306, 302), so the game sat on the title screen
while the script read counters as though a mission were running.

Re-run with the real drive, at the same point in the same mission:

| | claimed | measured |
|---|---|---|
| `OverlayPrepare` | "thirty callers and not one of them runs here" | **86**, once per frame |
| `SelectUnit` | "leaves the counter at 0" | **3** |
| shake timer while playing | zero | zero -- this one stands |

**`OverlayPrepare` does run**, and the rest of its account survives: the row
is always 0, so after the first call the "already on that row" test returns
and the tail is skipped. That is why three mutations to the tail passed --
not because the function is dead, but because its tail runs ONCE per session.
The claim to withdraw is "it does not execute", which came from zeroing a
global at the top and reading it back on the TITLE SCREEN and then
generalising to a mission.

**`SelectUnit` does run**, three times in a Boot Camp mission. What survives
is the separate finding that `group` appears only under `data/mp*` -- that is
about the SCRIPT route and was measured from the shipped data, not from the
broken probe.

**The check that would have caught it costs one line.** `ADDR_GAME_CLOCK_MS`
read 0 in every one of those runs, and this project already records that it
ticks in play. Dump a value that is known to change before believing anything
a probe says about a state it claims to be in.

**And the map descriptor is now measured rather than guessed.** In Boot Camp
it is `{cols 16, rows 16, shift 4, cells 0x02B53F40}` -- a SQUARE map, with
the shift exactly `log2(cols)`. So the object walker's clamp of the bottom
edge against `cols - 1` rather than `rows - 1`, recorded below as an open
question, is indistinguishable on this map. It stays open: it needs a
non-square map to settle, and whether the game ships one is not yet known.

- **`RowRelease` (0x0041D3A0), and one flag gates two actions.** A row that
  does not own a buffer is left entirely alone -- and in particular is NOT
  unregistered. So `+0x34` means "this row is in the map's cell lists AND owns
  an allocation", the two together, which is what makes one test enough for
  both. The order is unregister, free, clear: taking the row out of the cell
  lists before the buffer goes is the part that matters, because anything
  walking those lists in between would reach freed memory.

  It also corrects a comment: the unregister it calls is `0x0041DB20`, not
  `ADDR_ROW_UNREGISTER` (`0x0041D480`) as `orig.h` implied. Different
  functions -- this one takes the row and the descriptor and no index.

- **Unexercised, for the reason CLAUDE.md already gives.** Its caller
  `FreeSubrecordRows` is ours, so the counter is blind either way; what
  decides it is that nothing in an observed Boot Camp or campaign window
  dies, so `FreeItem` and the whole unlink family never run. A mutation
  before the first guard changes nothing.

## The next target, read but not written

**`0x0041E440` and `0x0041E160` are the map's object painter, and between
them they are the last large thing in the render path that this environment
definitely exercises.** Recorded here because the reading is done and the
writing is not.

`0x0041E440` (592 B, three callers, called from our own `PaintMapTiles`)
walks the map's cell grid. It clamps the world rectangle to the map -- shifted
right by 8, so these are tile indices -- and the clamp on the BOTTOM edge is
against `cols - 1`, not `rows - 1`, which is either a bug or a square-map
assumption and is worth settling before transcribing. Each cell holds a linked
list of `{obj, ?, next}`; each object is filtered by a predicate at
`0x0040A040` and by two "did we already pass this edge" tests, then handed to
`0x0041E160`.

`0x0041E160` (736 B) is the DEPTH SORT: it inserts the object into a sorted
list of at most 500 (`0x1F4`) twelve-byte nodes at `0x00507350`, with the head
index in `0x0050B1D8` and the count in `0x0050B1D4` -- the three globals
`0x0041E440` clears on entry. The walker's tail then draws that list in order
through `0x0040A090`.

So the shape is collect-then-sort-then-draw, and the three globals are the
sort's state rather than draw counters. Both functions run constantly in a
mission and a mistake in either is worth tens of thousands of pixels, which
makes them well-chosen and worth doing carefully rather than quickly.

- **`StateLeave` (0x0042E720): two movie pointers, and they are not the same
  one.** It owns `ADDR_STATE_MOVIE` (`0x00515F98`) while `MovieForget` clears
  `ADDR_MOVIE_CURRENT` (`0x006568A0`) -- so the call to `MovieForget` is not
  the teardown, it is the OTHER global being let go first, and the stop and
  delete below act on this one. Reading it as one pointer would have made the
  second null test look like a repetition of the first.

  It is not. The global is re-read between them, and `MovieForget` is a call:
  nothing here establishes that it cannot reach something that clears this
  pointer too. Both tests are reproduced.

  The primary surface is cleared to colour zero on BOTH paths, with a movie
  and without, so leaving a state always blanks the screen.

- **Unexercised, and the counter could not have said so.** Its four callers
  are state transitions; two of them are now ours, so `StateLeave`'s counter
  is 0 by construction and the other two never fired. What settled it was a
  mutation: clearing the primary to colour 200 instead of 0 changes nothing
  in `bootcamp` or `movies`. The `WndProc` arm that reaches it needs
  `g_gameState != 0`, and the intro -- the obvious candidate -- runs in state
  0 and takes the other branch. `0x00515F98` reads null in every
  configuration tried.

  The rule from two commits ago says to check reachability BEFORE writing.
  For a function nothing has patched yet there is no counter to read, so the
  check available was "do its callers run", and that was not decisive. The
  one that is decisive costs a build: patch it, mutate something
  unconditional at the top, and look.

- **`RepaintDirtyList` (0x0041D000), and the third draw counter is not a
  counter.** It walks the registered dirty rectangles, clips each against the
  region it is given and repaints the INTERSECTION -- an entry half outside
  the region redraws only the half inside.

  The list is 20-byte records chained through an index at +0x12, and record
  ZERO is the head sentinel: the walk starts at `records[0].next` and stops
  when a `next` is zero, so index 0 is both the head and the end marker. That
  is what `0x00508AD6` is. `orig.h` had it as `ADDR_DRAW_COUNT_C`, one of
  "three uint16 counters ... what each counts is not established" -- which is
  exactly what it looks like from the sweep that clears the three together,
  and the walk is what settles it. `ADDR_DIRTY_HEAD` now, and the comment
  says which two are still counters.

- **Its coverage cannot be settled by sampling, and that is a property of the
  data.** The walk itself runs whenever the view scrolls -- `ScrollMapCache`
  calls it unconditionally on that path. Whether the LOOP BODY runs is
  another question, and the honest answer is that the list is built and
  cleared inside a single frame: `ADDR_RESET_DRAW_COUNTS` empties it at the
  top of every frame, so anything sampling from the control thread sees zero
  almost by construction. Forty samples during a scrolling mission came back
  non-empty zero times.

  So the entry and the empty-list exit are exercised and the body is verified
  by reading. Worth stating plainly: a probe that CANNOT see a thing is not
  evidence the thing does not happen.

- **The level table, its lookup and the three files that fill it.** Five
  functions in one band: `FreeLevelTables` (0x0043E8B0), `FindLevelRecord`
  (0x0043E1F0), and `ReadCampaignLevels`, `ReadMpMapList` and
  `ReadBootcampLevels` (0x0043EC80, 0x0043ECC0, 0x0043ED00) -- one shape three
  times, differing only in the filename.

  Each reader empties the tables, chdirs to whatever the shared scratch buffer
  holds, and parses. A failure is logged and nothing else: the tables are left
  empty and the caller finds no record.

- **The reset is what identifies the SECOND table.** `0x0043E8B0` owns two
  `{base, count, capacity}` triples side by side -- the level records, and the
  registry `ADDR_SCRIPT_LIST_FIND` searches. When that searcher was named, two
  commits ago, its comment had to say the registry "has not been identified":
  a lookup alone cannot say who fills it. The function that FREES it can, and
  it is loaded from the same `.txt` by the same reader.

- **The bsearch key is a whole 0x30C-byte record with one dword set.** The
  rest is never initialised and never read, because `CompareDword` looks at
  that one field. Reproduced as written rather than shrunk to a four-byte key,
  which would be a different function the day the comparator grows.

- **Seven seams closed at once, which is the most this session.** Five
  `orig_` macros across `dplay.cpp`, `startgame.cpp` and `widget.cpp`, plus
  two spellings of a reconstructed comparator reached through the image from
  `map.cpp` itself. `defparse.cpp`'s private `bsearch` typedef moved to
  `orig.h` at the same time, because two modules want it now.

- **Looking up the wrong id is a seven-line log difference.** `id + 1` in the
  key leaves `bootcamp` without its map: the object-link complaints, the
  "freeing temporary map load data" line and everything after it are simply
  absent, because no record was found and no level was selected.

- **`ScrollDecay` (0x0042B420) is the screen SHAKE, and it is the view
  rectangle that shakes.** One step per frame, and the first thing
  `ComposeFrame` does. The X offset is added to left and right and the Y
  offset to top and bottom, so the whole rectangle moves and nothing is
  scaled or re-centred -- which is what settles what the four globals at
  `0x00514E14` are being used for here.

  The timer is counted down by the per-frame delta beside the game clock.
  When it runs out everything is zeroed -- timer, both phases, both steps and
  the amplitude -- so the view returns exactly where it was and no residue is
  left for the next shake to inherit. The amplitude fades only over the LAST
  1024 ms; above that the shake is at full strength, and `(amp * left) >> 10`
  is an arithmetic shift, so the taper is linear rather than smooth.

  Each axis advances its phase by `step * rate`, bounces off `+/-amp`
  reversing the step, and contributes the truncated whole-pixel offset. The
  phases are floats and the steps are integers, which is why the whole thing
  is done on the x87 stack.

- **Half of it is worth 122,875 pixels and the other half never runs.**
  Adding a constant four pixels to the view rect before the early return puts
  `bootcamp` that far out against a budget of 500, so the function executes
  and the rectangle really is what it moves. But a probe reads the shake
  TIMER as zero for sixteen seconds of live Boot Camp, so only the
  early-return arm is taken: the oscillation is verified by reading. Same
  shape as the palette expander's second table, one commit earlier.

- **This one was chosen by asking whether it runs FIRST**, which is the rule
  the previous commit had to write down. `ComposeFrame` calls it
  unconditionally on every frame and `ComposeFrame`'s own counter is in the
  tens of thousands, so reachability was settled before a line was written.
  The counter for `ScrollDecay` itself reads 0, which is the ordinary blind
  spot and not a surprise.

- **`SelectUnit` (0x00427CE0), and the cap is 65 rather than 64.** Fifteen
  callers -- the HUD, the script and the unit code. Three refusals and then
  two writes: a null object; a list already OVER sixty-four, which is
  `> 0x40` and not `>=`, so sixty-five is the real cap; and a uid already in
  the list. Past those the uid is appended and the object is marked with bit
  0x400.

  The duplicate scan compares UIDS, not object pointers -- the list holds
  uids and so does the comparison, which is the part that would be easy to
  get wrong.

- **`ADDR_SHUTDOWN_OBJ` named a call site, not an object.** `0x00512308` is a
  `{capacity, count, items}` ptr list of object uids; all the old name knew is
  that `ShutdownSubsystems` empties it. It is `ADDR_SELECTED_UIDS` now, on the
  evidence of the cap, the deduplication, the per-object bit, and the consumer
  at `0x00427990` that walks it with our own army.

- **The natural name was taken by Win32.** `SelectObject` is a GDI function
  that `wingdi.h` declares and `dllmain.c` sees, so the reconstruction is
  `SelectUnit`. The compiler caught it -- "conflicting types for
  'SelectObject'" -- which is the third name clash this session that a build
  found rather than a check.

- **It is unexercised, and this time the pre-test was available and I did not
  run it.** Boot Camp with the mouse clicking and right-clicking over live
  gameplay leaves the counter at 0. The script route is the `group` action,
  and `group` appears in the shipped scripts ONLY under `data/mp*` -- the
  multiplayer maps this environment cannot open a session for. Grepping the
  data for the keyword would have said so before the function was written,
  in seconds. That is a cheap check and CLAUDE.md has it now.

- **`SelectLevel` (0x0043ED50), and the reason `StopNamedSound` never runs.**
  Ten callers -- the Boot Camp button, SELECT MAP's OK, the multiplayer panel
  and the state-2 entry all go through it. Seven strings out of the level
  record into the globals the loader reads, and one flag. Nothing else: the
  record stays where it is, nothing is validated, and a null record is the
  only refusal.

  One of those globals is `0x00511D58`, the buffer `StopNamedSound`'s only
  call site guards on being non-empty. CLAUDE.md has recorded for a while
  that it stays all-zero for an entire Boot Camp mission. It stays zero
  because THIS is what fills it and Boot Camp's level record leaves the field
  empty -- not because nothing writes it. The record's field is a level
  property, so a level that names a sound would reach that call.

  `0x00511CC8` is `ADDR_TILESET_RESERVE`, already named from the tileset
  loader, so the flag at +0x244 is "reserve the first ten palette entries".

- **`LEVEL_OFF_NAME` was already taken, by a different field.** The record's
  DISPLAY name is at +0x44 and already carries that name; the MAP name is at
  +0x004. The compiler caught the redefinition. Mine is
  `LEVEL_OFF_MAP_NAME`, and the comment says which is which.

- **Dropping one copy is a log failure, not a pixel one.** Leaving the map
  FOLDER uncopied puts `bootcamp` three messages apart -- "Unable to open
  object data file", "Failed to open Object.aai for reading", "Couldn't parse
  Object.aai!" -- because every data file after it is opened by bare name
  from a directory that was never entered. The function's output is a set of
  paths, so the evidence is what fails to open.

- **The palette loader, and a second table that is a copy of the first.**
  `0x0041B6D0` reads an 8-bit `.bmp`'s HEADER and palette and nothing else --
  the pixels are never touched -- and expands it into the pair of 256-entry
  tables the renderer wants. Three functions: the file half (`0x00422F60`),
  the expansion (`0x0041AEB0`) and the pair.

  `biClrUsed` of zero means 256, which the original writes back into the
  caller's header before using it; anything not 8 bits per pixel is rejected
  after the header is already read.

- **The two tables come out IDENTICAL and that is not a misreading.** The
  second is built by reading the first back byte by byte and dropping the top
  one -- but `SwapColourBytes` has already zeroed that byte, so the mask
  removes nothing. What the second is FOR is the copy that survives: the
  first is the working palette and gets written over, the second is the
  pristine one to restore from.

  The expansion count is a fixed 256 and does NOT consult `biClrUsed`, so a
  file declaring fewer entries still expands whatever the reader left behind
  them.

- **One half is worth 128,570 pixels and the other nothing at all.**
  Dropping the channel swap puts `bootcamp` at 128,570 differing pixels
  against a budget of 500. Zeroing the pristine table changes NOTHING -- not
  in `bootcamp`, `movies`, `menuscreens` or `intro`. Nothing this project can
  drive reads it, so that half stays verified by reading, and the asymmetry
  is stated rather than left for a clean suite to imply.

- **`OverlayPrepare` (0x00412D30), thirty callers and not one of them runs
  here.** It chooses the menu row the cursor animates and resets the
  animation and the two optional overlays with it. Two guards in front, and
  they are not the same: the first is a THROTTLE -- outside a net game, and
  unless the caller forces it, a row change is refused when one has already
  happened this millisecond -- and the second is the ordinary "already on
  that row" test. The stamp is written between them, so a repeated call on
  the same row still consumes the millisecond.

  The row's first sprite goes into the slot one past the sprite array, which
  is what `DrawMenuCursor` draws. `orig.h` called that slot
  `ADDR_MENU_SPRITES_END`, "one past; also cleared as a slot"; `surface.cpp`
  has called it `g_cursorSprite` all along and was right. The comment says
  so now -- the name is the address's arithmetic, not its job.

- **Three mutations passed and the third one is what explained the other
  two.** Picking the wrong sprite frame, and refusing to change the row at
  all, both left every configuration identical. Zeroing the cursor sprite at
  the very TOP of the function -- before any guard -- also changed nothing,
  and a probe then read the global back as non-zero. It does not run.

  `DrawMenuCursor` reads 25,999 on the same run while `DrawMenuOverlay`, the
  one reconstructed caller, reads 0: the cursor is reached by another route
  entirely and the overlay path is not taken. The other thirty callers are
  in-game cursor modes -- unit orders and the like -- that no drive this
  project has reaches, and a live Boot Camp mission with the mouse moving
  over the map leaves the counter at 0 too.

- **The front has moved into code this environment cannot drive, and that is
  worth saying plainly rather than one function at a time.** Five of the
  functions landed this session are unexercised: `ListDropOldest`, the three
  multiplayer row-colour ones, and now this. Two causes, both structural --
  the multiplayer panel needs a second player on a DirectPlay session this
  machine will not open, and the in-game cursor modes need gameplay the
  drives do not reach. They are verified by reading and by transcription,
  which is a weaker standing than everything before them, and CLAUDE.md's
  unexercised list is where that is recorded.

- **`MpPanelDestruct` (0x00430480), and a leak that nothing could see until
  the oracle was the REFERENCE COUNT.** The panel's destructor releases two
  sprite arrays and chains to the dialog base. Neither array is a field of
  the panel: both are globals, and their bounds are this function's loop
  limits rather than anything declared -- five sprites from one and thirteen
  from the other, and the first array's limit is the address of the message
  log, the next global along.

  Leaving the second array unreleased passed everything. The pixels, the log
  and the 128-node widget tree were all identical, and so was the REGISTERED
  SPRITE COUNT -- which was the obvious thing to reach for and is useless
  here, because every one of those sprites is still referenced elsewhere and
  the slot is never freed.

  What moves is the refcount inside the sprite. `ab.sh` reads the first
  array's pointer, dumps the dword at +4 and puts only that in the state
  file: 1 on a correct teardown, 3 with the loop disabled. The pointer itself
  is a heap address and stays out, which is the rule the widget dump already
  follows -- carry one real datum, never the pointer.

  **Reach for the count that the defect changes, not the count that is easy
  to read.** The registry total was the first thing tried and it answers a
  different question.

- **`MpPanelUpdate` (0x004316D0), and the field at +0x4C has a name now.**
  The panel's per-frame update greys the COLOUR and TEAM buttons row by row,
  and the policy is exactly the one their handlers guard on: a row holding a
  real player may be edited only if it is OURS, an empty row only by the
  HOST. Both buttons of a row always agree.

  That is what settles what +0x4C is. `widget.h` had it as `unknown4C`, "set
  disqualifies from focus", which is true and is what the two focus walkers
  do with it -- but a function that writes it per row on an editing policy is
  writing "greyed out". Renamed to `disabled`.

- **It is compared exactly, because `ctl widgets` already prints it.** The
  dump's `nofoc` column IS this field, so the sweep needs no pixels: making
  the team button disagree with the colour button flips `nofoc` on four
  nodes and fails the tree. That is the second time the widget dump has
  turned out to cover a function nobody chose it for.

- **The second sweep pushes five numbers into text every frame**, the score
  limit into the panel's own buffer and each army's setting into the row's
  inner edit box -- which is why those four fields cannot be typed into:
  anything a keystroke put there would be overwritten before it was drawn.

- **`FillListFromRules` (0x00430140), and the only place the game's FILE is
  not opaque.** It clears a list box's rows and refills them from a text file
  in `rules/`, one row per line, three callers each with a different
  filename. The line keeps its newline: `fgets` gets the whole 0x100 buffer
  and nothing trims.

  The EOF test is inline -- the MSVC `_flag` byte at +0x0C against `_IOEOF`
  -- rather than a call to `feof`, so reproducing the loop means reading the
  same byte. `orig.h` has kept the game's FILE opaque on the argument that we
  never dereference it; that is now true everywhere but here, and the two
  offsets are recorded with the reason rather than a struct pretending to be
  an `_iobuf`.

  The test comes BEFORE the first `fgets` as well as after each one. A file
  that opens already at EOF therefore clears the list and adds nothing, which
  is a different answer from leaving it alone -- and is why the reset comes
  first.

  It runs once per panel open (`FillListFromRules=1` on a probe) and adding
  no rows puts `mpoptions` 1,966 pixels out against a budget of 300.

- **The player row's two colours, and what they are chosen by.** `0x00432C50`
  picks the INK by how the CONNECTION is behaving and `0x00432CE0` picks the
  PAPER by whether the player is READY -- two questions, not one, on the same
  row.

  The ink degrades in three cumulative steps for everyone but ourselves:
  latency over 750 ms, over 1000 ms, and then a fourth colour that is not
  latency at all but SILENCE -- the player's record stamps `GetTickCount` at
  +0x70 on every packet, and 1250 ms without one overrides whatever the
  average said. The paper's first arm is the host's alone: a row that has not
  confirmed the map gets its own colour, and everyone else falls through to
  the ready pair.

- **`CommMean32` was documented as averaging the COMM OBJECT and it averages a
  PLAYER.** Its one caller passes the result of `FindPlayerById`, and so does
  `PlayerLatency` (`0x00402EC0`), which is the same mean reached by id. The
  header said "thirty-two samples on the comm object is the shape of a latency
  or rate average" -- which was as far as reading that function alone could
  get, and wrong about the object.

  What settles the MEANING is the callers rather than the body: both compare
  the answer against 750 and 1000 to choose a colour. It is milliseconds.

- **Three of the four are UNEXERCISED and the fourth runs 60,152 times.** The
  row painter has two branches and with nothing connected it takes the other
  one, so `MpNameInk`, `MpNamePaper` and `PlayerLatency` all read 0 -- while
  `MpNameSetInk`, which the same painter shares with two other call sites,
  reads 60,152. Reaching the branch needs a live DirectPlay session with a
  second player, which this machine cannot open.

  Measured rather than assumed: disabling the paper's no-map arm entirely
  changes NOTHING in `mpoptions`, on any of its four frames. That is what
  sent me to the counters, and the counters said the function never runs --
  ask whether the code runs before asking whether the term matters.

- **The TEAM button click was a race, and shortening the tap did not fix
  it.** Two `mpoptions` runs in a row came back with a team sprite one step
  apart on an otherwise identical tree, the rest clean -- about one in five.
  The first theory was auto-repeat: the button repeats after 250 ms held and
  the socket's default tap is 120 ms *released on a poll*, so a late poll
  could step it twice. An explicit 40 ms hold did not stop it, which is what
  says the theory was wrong.

  What the value actually is, is a pure function of how many clicks landed,
  and the flaky one was on a row we do not own -- so it is a driving race and
  not a reconstruction defect. The right-click moved to row 0 and row 1's
  team click is gone: the guard it covered is the same one row 1's colour and
  name already take, so the coverage is unchanged and the race is not there
  to lose. Three consecutive runs identical.

  Worth stating as a rule rather than a fix: **a tree compared with no budget
  at all cannot afford a driving race**, and the way to tell a race from a
  defect is that a race is intermittent on an unchanged build.

- **`OnChatEnter` (0x00431CE0), the panel's chat line.** Log the text locally
  in our own army's colour, broadcast it with `system` zero so the sender byte
  is the army rather than the announcement colour 4, empty the field, repaint.
  It is referenced nowhere in the image except one `push 0x431ce0` inside the
  panel constructor, which is why `callsites.py` reports zero callers -- the
  `push imm32` shape this project has been caught by twice.

- **The field is not cleared, it is overwritten from `ADDR_DIR_SCRATCH`** -- a
  shared `char[]` with eighty references across the image. It is empty at that
  moment and the effect is a clear, but that is a property of what ran before
  rather than of this code. Reproduced as written; a `text[0] = 0` would be a
  different function.

- **Driving it needed a WM_CHAR that the socket could not send.**
  `EditCharHandler` fires the field's handler on `ch == 0x0D` and on nothing
  else, and `key 1c tap` injects DirectInput, which never produces a WM_CHAR
  at all -- so RETURN through the keyboard path is invisible to an edit box.
  `type` grew one escape, `\r`, and a literal backslash is not in the edit
  box's whitelist so it cannot collide with anything typeable.

  Before that the probe read `OnChatEnter=0` with the field focused and the
  text typed, which reads exactly like a broken handler and was a missing
  message.

- **`mpoptions` now types a line and sends it**, and the chat record comes
  back `03000000 08010000 01 "Zulu"` on both sides -- the sender byte is the
  army, where the OK click's is 4. Passing `system` as 1 instead of 0 fails
  it. The colour and the text never reach the screen in a form the pixel
  check could resolve, so this is the record or nothing.

- **`ListDropOldest` (0x004539A0), and it is UNEXERCISED.** It is the other
  end of `ListAdd`: drop row zero. It does not memmove in place and it does
  not realloc -- it allocates a fresh array of the new size, copies rows
  1..n into it, frees the old one and swaps, so the array is rebuilt on every
  trim. The count is decremented FIRST, which is what makes the loop copy
  `count` rows starting at row 1 rather than `count - 1`.

  Its one caller is `MenuMessage` and it fires only above a hundred logged
  lines. No drive this project has posts a hundred menu messages, so nothing
  in `ab.sh all` executes it and the counter is 0 twice over -- once for the
  blind spot, once because it genuinely never runs. It is verified by reading
  and by mirroring `ListAdd`, which IS exercised, and that is stated here
  rather than left to be assumed from a clean suite.

  Two details worth keeping. When the count reaches zero the allocation still
  happens, with a size of zero, and the pointer is stored -- reproduced,
  because a caller that then appends expects a block it can realloc. And the
  discarded row's value pointer is freed only when +8 says the list owns it.

- **`MenuMessage` (0x00431C30), and its third argument is not a flag.** It
  logs a line in the menu's message list -- a string list capped at 100,
  trimmed one at a time from the oldest end -- and then makes the panel show
  it. The last thing it does is `BlinkerStart(widget, 100, 20)`, and the
  third argument picks WHICH of the panel's two indicators flashes:
  `Announce` passes 0, the host-migrated handler passes 1. A message about
  the game and a message about the session light different lamps.

  That reading came free from the alias ratchet. I had named `0x00456DC0`
  `ADDR_CHATBOX_SCROLL_END` from the two constants pushed into it; it has
  been `ADDR_BLINKER_START` since the widget vtable survey, and the name
  already there knew what the call was. Second time in three commits that
  the old name was the informed one.

- **It reads the panel's chat widget out of the CURRENT SCREEN without
  checking that the current screen is a panel.** Reproduced. Every caller in
  the image is a panel path or in-mission, and a guard here would be a
  different function -- but it is worth knowing the field offset is applied
  blind.

- **The seam it closed ran through three modules and two of them are flat.**
  `orig_menu_message` was in `misc.cpp`, `commmsg.cpp` and `winproc.cpp`.
  `MenuMessage` calls `BlinkerStart` and `ListAdd`, which live in
  `win32/widget.cpp`, so it cannot sit in a flat module that includes
  `widget.h` -- that header reaches `LPDIRECTDRAWSURFACE` through the sprite
  in a widget and `checksplit.py` would fail it. Both are declared
  `extern "C"` in `commmsg.cpp` instead, which is the seam that file already
  uses for three comm methods.

  `Announce` moved out of `misc.cpp` with it, because `misc.cpp` is in the
  offline test's link and `commmsg.cpp` is not.

- **Two mutations, and only one of them is caught.** Returning before the
  log puts `mpoptions` at 327 pixels against a budget of 300 -- real signal,
  and a thin margin, because a chat line in a menu font is small. Swapping
  the two blinkers changes NOTHING: 0 pixels on all four frames. Twenty
  flashes at 100 ms is two seconds and the shot lands four seconds after the
  click, so the lamp is already still. Which indicator blinks stays verified
  by reading, and that is a timing gap rather than a coverage one.

- **`SendChatMsg` (0x00411E90), and the name it was carrying was wrong.**
  `orig.h` had it as `ADDR_CHAT_APPEND`, which is what `Announce`'s second
  call looks like from where it sits -- put a line on the menu, then append it
  to the chat log. The body does not append anything: it stamps a static
  record at `0x004FA910` with a sender byte and the text and hands it to
  `SendGameMsg`. The FIRST call is the local append. Renamed, and the comment
  in `misc.cpp` that described the wrong behaviour went with it.

  Third instance this session of a name taken from a call site being wrong
  about the body, and the second where the OLD name was the one that had to
  go rather than a new one beside it.

- **`AnnounceToPanel` was `Announce`, reconstructed in `misc.cpp` months ago.**
  I had read `0x00430120`, named it, written it and wired it in before
  `checkpatches.py` refused the build: "0x00430120 patched 2 times". Fifth
  near-duplicate the project has stopped, and the second in two commits. The
  rule that would have caught it earlier is the one already written down --
  grep the tree for the ADDRESS as well as for the name -- and it is cheap.

- **It truncates in the caller's buffer**, at `[0xFE]` and not at `[0xFF]`, so
  the signature is `char *`. `Announce` takes `const char *` and hands its
  argument straight through, which is a promise the callee does not keep; the
  cast is written out with a comment rather than hidden, because the two
  callers that pass a stack buffer would really lose the tail.

- **The record is the evidence and the screen is not.** With nothing
  connected the send goes nowhere, so clicking OK on the multiplayer options
  shows only the local half. `ab.sh`'s `state` artifact reads the record
  instead: `03000000 08010000 04 "Options changed by host."`, identical on
  both sides. Stamping sender 5 instead of 4 -- invisible in every frame --
  fails it.

- **The map-selection refresh and the three checksums under it.**
  `0x004301D0` is what runs when the chosen map changes, and the four
  functions below it are the data-file checksums a multiplayer session
  compares before it will start: the rules `.aai` files, the mission script,
  and the map xored with the object table it was authored against.

  None of it is cryptographic and none of it is meant to be. It catches a
  different EDITION of the data, not a tampered one, which is why an XOR fold
  of 32-bit words is enough.

- **`Checksum` was already reconstructed, and the alias ratchet is what said
  so.** I had written `ADDR_FILE_CHECKSUM` on `0x0042DBB0` and was about to
  write the body a second time; `map.cpp` has had it as `Checksum` since the
  savegame work. Four more names in the same batch were duplicates of ones
  already in `orig.h` -- `ADDR_FOPEN`, `ADDR_FCLOSE`, `ADDR_FREAD` and
  `ADDR_MODE_RB`, which I was about to add as a CRT seam that already exists.

  Five collisions in one edit, all caught before the build. This is the
  fourth near-duplicate reconstruction the project has stopped, and the
  fourth different mechanism to stop one.

- **`ADDR_SCRIPT_FIND_NAME` was already taken, by a different function.**
  `0x0043F670` searches the script NAME table; `0x0043E900`, which
  `MpScriptChecksum` guards on, lower-cases its argument in place and
  searches a different registry entirely at `0x00656344`. The compiler
  caught the redefinition. The second one is `ADDR_SCRIPT_LIST_FIND` and the
  registry it reads is not yet identified -- said plainly rather than
  papered over with a plausible name.

- **`FileExists` opens the file.** There is no `GetFileAttributes` anywhere
  in this image: all its file handling is CRT, so an existence test is
  `fopen` then `fclose`. It opens `"r"` where the checksum opens `"rb"`,
  which reads like an oversight and is not worth improving on -- nothing is
  read through the handle.

- **The refresh serves four callers and only one of them has a panel.** The
  thumbnail is fetched only when the repaint object exists AND the game is on
  the menu AND the menu mode is 7, the host panel; the other three callers
  fall through the whole widget half. That triple test is not defensive
  coding, it is the function's structure.

- **The three checksums are compared exactly, by reading the globals.** They
  go out in the handshake and never reach the screen, and the game's own
  logger is stubbed to `ret` in this build -- so the "Checksum of %s is %x"
  lines the code writes go nowhere and neither the log nor the pixels can see
  a wrong total. `ab.sh` grew a `state` artifact for this: twelve bytes at
  `0x00511CCC`, dumped over the control socket on both sides and diffed.

  It reads `df072909 717e6220 2d690575` -- map, script, rules -- identically
  on both sides. Dropping `weapon.aai` from the rules fold changes the last
  word to `3b775438` and the check fails, so it discriminates.

- **And the panel now gets a map-list row clicked.** That is what
  `RefreshMapSelection` is FOR -- the "map changed" and "game type changed by
  host" arms both call it -- so the state dump is taken again afterwards and
  appended.


- **The multiplayer panel's own tree was never compared, and now it is --
  before and after clicking it.** `mpoptions` built the panel, walked straight
  past it to the OPTIONS dialog, and dumped only that. The panel is 44 nodes:
  four player rows of {name, toggle, spinner, message} plus the map list, the
  chat field and their bars.

  It now dumps the panel, clicks row 0's toggle once and its spinner twice --
  134..152 and 191..209 by 39..59, measured from that tree -- and dumps it
  again, appended. 128 nodes over three dumps, identical on both sides.

  **The clicks demonstrably do something**: 19 lines move between the two
  panel dumps, the toggle's sprite id going 1574528 to 1574529 and the
  spinner's 1575808 to 1575811. A handler that changed the wrong row, or
  nothing, would show up as a different 19 lines.

  These are the only clicks in the suite that reach the three MP button
  classes' HANDLERS. Those are still the original's -- only the constructors
  are ours -- so what this checks today is that our widgets behave correctly
  under the original's handlers, which is exactly the check the constructors
  needed. It also sets the handlers up as the next unit: `0x00432D50`,
  `0x00432EC0`, `0x004330E0` and `0x00433190`, each around 200 bytes, guarding
  on host-ness and the slot count and reaching `CommSendPlayers`.

- **The multiplayer panel, started from the LEAVES.** `0x00430530` is 4,497
  bytes and was declined twice as too big to start between two other things.
  It builds three button classes, one of each per player row, and those are
  ordinary widget constructors of the kind this project has done a dozen of:
  `0x004329A0` (0x74), `0x00432E20` and `0x00433030` (0x68 each). All three
  are reconstructed now, and the panel is still the original's -- so they run
  in the middle of a live path and `ab.sh mpoptions` compares the result.

  **A 4.5 KB root is a bad first step and its leaves are a good one.** Nothing
  had to wait for the layer above it, which is the same shape as the script
  handlers calling the original's parsers.

- **The names are from the SHAPES and nothing in the image says otherwise.**
  All three derive from the base button and all three carry the row they
  belong to in the base's `0x0058`, which is how their handlers know which
  player they are for. The first takes a string and two ink bytes and is the
  row's name; the other two are 18x20 at a column the caller picks, and the
  one with a RIGHT handler and auto-repeat is a spinner where the one with
  only a left handler is a toggle. That difference is the whole distinction
  between them.

  `0x004329A0` is also the only widget constructor here that takes its
  rectangle as FOUR SEPARATE ARGUMENTS -- nine stack arguments in all -- and
  writes it into the base directly instead of going through `RectSet` first.
  Same result, one fewer call.

- **Measured: 4, 4, 4.** A probe down the host-panel path reads
  `MpNameConstruct=4 MpColourConstruct=4 MpTeamConstruct=4`, one of each
  per player row, with `ButtonBaseConstruct=0` beside them -- the usual blind
  spot, since all three call it by name. Worth doing because `ab.sh multi`
  does NOT reach the panel: its START A WAR opens ENTER BATTLE NAME, and it is
  `mpoptions`, poking menu request 7, that builds the thing.

- **`FreeSubrecordRows` and `ItemPreDestroy`, and one structure seen two
  ways.** The sub-list header at `OBJ_OFF_SUBRECORD` is
  `{?, count, rows, capacity}`, so the count the object reads at
  `OBJ_OFF_ROW_COUNT` (0x70) and the header's own `+4` are the SAME dword --
  which is why `TakeOffMap`'s offsets look four bytes off from
  `FreeSubrecordRows`'. Naming both shapes rather than picking one is what
  makes the two functions read consistently.

  `0x00434EC0` frees the array only when there is one, and clears the CAPACITY
  unconditionally -- so an already-empty list still gets that written over it.
  Neither the count nor the header's first dword is touched.

- **`ItemPreDestroy` has three things worth not tidying.** `0x0042A0A0`
  unlinks an object from every cell list it is registered in: a byte count at
  `0x8C`, `0x10`-byte entries at `0x90`, each holding the index of the list it
  is linked into or -1, and each unlink writes -1 back -- so a second call
  returns on the first entry.

  The count is re-read every iteration though nothing changes it. Entry ZERO's
  index is tested BEFORE the loop as well as inside it, so an object whose
  first entry is already unlinked leaves the rest linked. And the second
  `test al, al` cannot be taken at all: the first already returned on zero and
  `jbe` on a byte asks the same question.

- **A python edit inserted the same function body twice and the COMPILER
  caught it, which is the second time this session an edit script went wrong
  in a way only a downstream check saw.** The first was `str.replace("", x)`
  turning STATUS.md into 529 MB, caught by GitHub's push limit. Here it was a
  redefinition error. Both were silent at the point of the mistake.

  The pattern is the same: a scripted edit whose match did not do what the
  script assumed. Reading the diff for `^+void __cdecl` -- two lines where one
  was intended -- located it in one command.

- **`TakeOffMap`'s two flags are not symmetric, and that is the whole design.**
  `0x004296E0` raises `OBJ_FLAG_OFF_MAP` (0x0800) UNCONDITIONALLY -- it is what
  eight callers test to know the work has been done -- and then gates the work
  itself on `OBJ_FLAG_ON_MAP` (0x0200), which it lowers. Call it twice and the
  first bit goes up twice while the rows are unregistered once. Reading either
  flag as "the" off-map flag gets one of the two calls wrong.

  The row loop re-reads the count every iteration and clears bit 1 of each row
  BEFORE unregistering it, in that order, because the unregister reads the
  row's flags.

- **`CommFindPlayer` returns a stored FIELD where the loop counter would do,
  and the two are not the same thing.** `0x0040F330` walks the comm object's
  player slots for a DirectPlay id and hands back `AM2_PLAYER_INDEX` -- the
  slot's own index field at `+0x20C`, four bytes before its army -- rather
  than `i`. They agree in every state this project has driven, and they are
  separate fields, so the reconstruction reads the stored one because the
  original does. Writing `return i` would be a guess that happens to work.

- **`ArmyInPlay` carries the neutral-army special case a third time.**
  `0x0040F920` resolves a uid to its army through `UidOnWire` and `UidArmy`,
  and army 4 answers YES without touching the comm object at all -- the same
  shortcut `CommArmyOfSlot` and `CommSlotForArmy` both have. Three functions
  with the same exception is the object model saying something: slot 4 is not
  a player.

- **`ColourDistance`, `SpriteSlotOf` and `UidObjKind`, and a comment in
  `orig.h` that was wrong in three ways.** It said the sprite registry is "a
  count at 0x006598C0 and a table of AM2_Sprite* at 0x006598C4; the lookup
  walks it for a matching id". Reading `0x00445990` instead: there is a
  CAPACITY at `0x006598BC` as well, the `AM2_Sprite*` table is indexed by SLOT,
  and a SEPARATE table of `{id, slot}` PAIRS at `0x006598C8` is what the lookup
  reads -- by BINARY SEARCH, with an UNSIGNED comparison, returning the slot.
  Two arrays, not one, and the second exists purely to make the lookup
  logarithmic.

  `UidObjKind` hands `FindSlot` the address of its own first argument as the
  insertion-point out-parameter, because FindSlot wants somewhere to write even
  when it finds the entry. The slot written there is never read and the uid is
  not used again. Reproduced with a local rather than by clobbering the
  parameter -- the same thing without the trap.

- **`ColourDistance` moved to the FLAT half so `make selftest` can link it**,
  which is the first time a function has been placed by where it can be
  TESTED rather than by what it is about. It is pure -- three byte reads a
  side, no globals, no calls -- and `misc.cpp` already holds `SwapColourBytes`,
  so the precedent was there.

- **`tools/vectors.py` could not see a pointer kept in EBP, and that cost a
  whole function's vectors.** `REG` listed six registers and not `ebp`, because
  a framed function uses it for the frame -- but one WITHOUT a frame is free to
  keep a pointer argument there, and `ColourDistance` does. Its second argument
  classified as `scalar`, every generated call passed an integer where a
  pointer was wanted, and it produced **0 vectors at 21.4% coverage** rather
  than failing visibly. With `ebp` added: `p,p`, 71 vectors, **100%**.

  Safe because a slot is only recorded when a register is loaded FROM an
  argument slot, and `mov ebp, esp` is not that. Checked by diffing every
  classification in the validation set before and after: one other function
  changed, `ClipRect`'s second argument from `s` to `p`, which is also right
  and also a pointer.

- **7,353 recorded vectors were 5,355 distinct, twelve functions had exactly
  ONE, and the cure was one uint32 per vector.** `MIN_VECTORS` exists because
  "one vector cannot distinguish a reconstruction from a coincidence" -- and
  it had been counting COPIES. `ObjFieldA`, `ObjFieldB`, `ObjFlagBit0`,
  `ObjFlagBit1`, `Field53C`, `IsKind7`, `CommMean32`, `MsgField12`,
  `TitleCaseName`, `ReturnZero`, `ReturnOne` and `ObjFlagClear0` were each
  recorded 82 or 96 times from a single input.

  The cause was the scratch: one deterministic pattern for EVERY vector, so a
  function whose only variation is behind a POINTER got the same call each
  time unless angr supplied the bytes. 96 tries collapsed to one.

  **Measured rather than reasoned, in both directions.** With `ColourDistance`
  at "71 vectors" and 100% instruction coverage, replacing `d1 * d1` with `d1`
  PASSED; replacing the whole body with `return 0` failed on the first vector,
  so the harness worked and the inputs did not.

  The fix is a SALT: the pattern is `((i*7+13) ^ (i>>11) ^ (salt*37)) & 0xFF`,
  the salt is the try index, carried as one uint32 per vector and recomputed
  on the replay side, so the bytes are never stored and the header does not
  grow. It is applied only where it can be OBSERVED -- a function with no
  pointer argument cannot read the scratch, and varying it there would put the
  duplicate count straight back as 96 "distinct" vectors that are one test.

  **6,852 vectors, every one distinct**, and the `d1` mutation now fails on
  ten of them. Three functions are still under `MIN_VECTORS` and all three
  earn it: `ReturnZero` and `ReturnOne` take no arguments and return a
  constant, and `BuildRgb332Palette`'s output does not vary with its input,
  which the tool already said.

  Coverage rose with it, because the inputs are real now: `ClipRect` from
  **ZERO vectors** to 12 at 36.2%, `PointInRect` from 43.8% to 56.2%, and
  `CompareTriple`, `ObjIsItem` and `ObjType2Field548` off their floors. **A
  count that includes duplicates is a claim about effort, not about inputs.**

- **`str.replace("", x)` inserts between every character, and a slice built
  from two `index()` calls has to be checked.** Rewriting the bullet above,
  the end marker matched an entry EARLIER in the file than the start, the
  slice came out empty, and the replace turned a 269 KB STATUS.md into
  **529 MB**. It committed cleanly and the push refused it -- GitHub's 100 MB
  limit was the only thing that noticed.

  Nothing else would have. `make check` does not look at STATUS.md's size,
  and a diff that large is not read. Assert `end > start` and assert the slice
  is non-empty; both are one line.

- **Four small functions off the FRONTIER, chosen by asking what our own code
  still reaches by address.** Listing every `orig_` macro in `src/game` with
  its target's size is a two-minute question and a better ranking than
  anything static: it names exactly the functions one call away from
  reconstructed code. 130 of them, and the four smallest useful ones went in.

  `PtrListGrow` and `PtrListShrink` (`0x0042A6B0`, `0x0042A710`) are the
  {capacity, count, items} record's two capacity moves and they are **not
  symmetric**: the grow adds twenty and reallocs, while the shrink takes
  twenty off and FREES the array outright when that leaves nothing, rather
  than reallocing to zero. So an emptied list gives its storage back and the
  next push starts from a null pointer, which `realloc` treats as a fresh
  `malloc`. Neither touches the count.

  `ItemsReset` (`0x00429450`) tears the object registry down, and passes 0 for
  `FreeItem`'s `unlink` -- which is the whole reason it can walk forward
  without the table moving under it, since unlinking is what memmoves the
  tail. It re-reads the count every iteration anyway.

  `WeaponByUid` (`0x0045EE80`) COMPLAINS rather than just refusing: three ways
  to fail and only one is worth a line. A zero uid and one that resolves to
  nothing return null in silence; one that resolves to a non-weapon logs "uid
  wasn't a weapon!".

- **The multiplayer host/join panel was looked at and declined for now.**
  `0x00430530` is 4,497 bytes, 115 calls, four player rows each with a colour
  multi-sprite, a name, a scroll bar and dots -- and it builds them through
  two widget constructors (`0x004329A0`, `0x00432E20`) that are not
  reconstructed either. It is the last screen and it is worth doing; it is not
  worth starting between two other things. Recorded rather than half-done.

- **MOVIES' page button retargets its buttons instead of rebuilding them, and
  does not repaint.** `0x0044E580` bumps the page, wraps past 2, and gives the
  four buttons that already exist a new slot index and the two sprites for it
  -- same NORMAL=b, FOCUS=a, PRESSED=a pattern the constructor uses. Nothing
  is marked dirty; they simply come out differently the next time the screen
  is drawn, which is what makes this cheap enough to do on a click.

- **`0x00452060` is the LOAD arm STATUS's open item 2 names.** It copies the
  chosen save into `ADDR_GAMEPROC_STR_B`, raises `ADDR_LOAD_PENDING` and asks
  for state 2 -- the whole of the load request, and now ours. What happens
  after that is still the puzzle: the flag is set, read as SET at
  `0x00425360`, and read as 0 again by `0x004255CB`. Reconstructing the
  requester does not move that, and saying so is the point.

  The name it sends is the SCREEN's copy, seeded by the constructor, so LOAD
  works without a row ever being clicked; an empty one is refused with wave 3.

- **Equal constants are not the same constant.** `ADDR_MENU_MODE` takes `0x0D`
  for the movie player and `AM2_MENU_REQUEST_MOVIES` is also `0x0D` -- but one
  indexes the 21-arm menu table and the other the 13-arm sub-state table, so
  reusing the name would have been the reverse of yesterday's mistake: one
  name on two different things rather than two names on one. `AM2_MENU_MODE_MOVIE`
  is a second macro on purpose, with the coincidence written down.

- **ENTER NAME's OK is left original, with the other two.** `0x00451990`
  globs the save directory to refuse a duplicate name and then creates it --
  CRT file I/O, which this port replaces wholesale rather than function by
  function. It joins DELETE PLAYER's OK and DELETE GAME's OK on that list;
  all three are the file layer rather than the menu.

- **`RepaintAncestor` is not `WidgetRepaint`, and the difference is the CLIP.**
  `0x00455C10` walks up to the nearest ancestor owning a sprite and paints
  THAT widget clipped to **this** widget's rectangle -- so only the area this
  one covers is redrawn, by whoever owns the background under it. With no such
  ancestor it paints itself. The clip rectangle it is handed is ignored either
  way; the signature exists so it can sit in a paint slot.

  All four arrow handlers end in it, so `audiovol` and `menuscreens` run it.

- **SELECT MAP's row callback dereferences twice.** `0x0044DEA0` reads the
  row's VALUE dword, which the constructor set to a `malloc`'d `int32_t *`
  holding the level id -- so the id is two dereferences away, not one. It then
  looks the record up and starts that mission. It also re-reads the row count
  after choosing the level and only then asks for state 2, a second check of
  what it has already tested; reproduced rather than tidied.

  **It stays verified by reading, and the reason is that exercising it starts
  a mission.** Clicking a row here is not a menu action; the callback ends in
  `RequestState(2)`. Putting that in a menu configuration would turn it into a
  gameplay one.

- **LOAD GAME's row callback is exercised and NOT discriminated, which is not
  the same thing.** `0x00451EA0` copies the chosen save's name into the
  SCREEN's own slot. `ab.sh campaign` clicks the row now -- but `save/sarge`
  holds one `.sav`, the constructor already seeded the same name from the same
  row, and neither the widget tree nor the log shows that slot. A callback
  that did nothing would pass. Two saves would fix it and would mean the suite
  carrying a fixture nobody created on purpose, so the limitation is written
  into `ab.sh` beside the click instead.

- **The save-game family's buttons, and a rule that nearly got broken on
  CONSTANTS rather than addresses.** Five handlers: ENTER NAME's CANCEL
  (`0x00451AC0`), LOAD GAME's BACK, DELETE and NEW (`0x00452010`,
  `0x00451F10`, `0x00451FB0`) and DELETE GAME's CANCEL (`0x00450180`).

  Every one that can be opened from a mission has the two-armed ending the
  OPTIONS dialogs have -- an overlay MODE in state 2 and a menu REQUEST
  otherwise. ENTER NAME's CANCEL is the exception and has only the request,
  because RECRUIT is not reachable from play.

  **`0x00659F58` is the save DELETE GAME is about to remove**, and the three
  handlers around it make a chain: LOAD GAME's DELETE copies the chosen name
  in, DELETE GAME's CANCEL clears it, and its OK reads it. The name is the
  SCREEN's own copy at `0x68`, which the constructor seeded from the first
  row -- so DELETE works on a screen nobody has clicked.

  **DELETE GAME's CANCEL is the only mode in the family that is computed**:
  `sete` on `mode == 0x1D` and `add 0x19`, so 0x1A when it was asked from
  DELETE GAME and 0x19 otherwise.

  **Two names for one constant is the same mistake as two names for one
  address, and this batch nearly made it twice.** LOAD GAME's BACK targets
  mode `0x17` and DELETE GAME's CANCEL `0x1A` -- the same values
  `MENU_MODE_OPTIONS` and `AM2_MENU_MODE_DEL_PLAYER` already carry. Both of
  those names come from the first CALL SITE seen and may be under-specific;
  the mode is a sub-state index into the table at `0x00426230` and what each
  arm shows is not established. A possibly-narrow name beats a second name on
  one value, so the existing ones are reused and the doubt is written down.

  `OnLoadGameNew` is exercised by `campaign`, which clicks NEW to reach the
  map -- and it stores the level record's OWN id where SELECT PLAYER, doing
  the same lookup two screens earlier, stores the literal 1.

- **LOAD GAME, and the menu screen table is finished.** `0x004520E0` is the
  campaign's save picker and the second of the two screens built two ways --
  no panel in a mission, with the panel's offset (`0x7D` by `0x62`) folded
  into every rectangle, and a panel with a zero offset on the title screen.
  DELETE GAME is the other, and the two are the only ones.

  Its list comes off the FILESYSTEM like SELECT PLAYER's -- `save\<player>`
  globbed for `*.sav` -- and it seeds its own copy of the chosen name from the
  first row, so LOAD works without the row ever being clicked.

  **Two things differ between the layouts beyond the offset**, and reading one
  arm gets the other wrong: the screen's focused child is the PANEL when there
  is one and the LIST when there is not, and the second is written after the
  list exists rather than before.

- **Two widget trees in one file, which is how a configuration covers a second
  screen without a fifth shot slot.** `campaign` already walked through LOAD
  GAME on its way to the map and compared nothing about it: its pixel budget
  is -1, so its frames prove nothing, and a log cannot see a button in the
  wrong place. The tree dump is now APPENDED to the same file as SELECT
  PLAYER's, and since the comparison is a diff, two trees compare as two
  trees. 20 nodes, identical.

  Worth remembering as a technique: the four shot slots are a real limit and
  the widget file is not.

- **MOVIES is the only screen that builds its buttons out of SPRITES rather
  than bitmap names.** `0x0044DFA0` preloads twelve thumbnail PAIRS into
  `0x0064` -- set 3, indices `0xC8..0xCB` then `0xD2..0xD9`, a gap the screen
  does not care about because the pairs land in twelve contiguous slots -- and
  then constructs each button with the SCRATCH BUFFER as all three of its
  bitmap names before overwriting the three sprite fields from the pair. The
  names are never used; the construction only has to not fail.

  It clears `OWNS_SPRITES` afterwards, which is what stops the destructor
  releasing sprites the screen preloaded and still holds. And the pair is
  (a, b) with the button taking b for NORMAL and a for both FOCUS and PRESSED
  -- a twice, from two separate reads of the same slot.

  Three pages of four, `ADDR_MOVIE_PAGE * 4 + slot`, with the three later
  thumbnails and the page button each gated on how many movies are unlocked.

- **On a fresh profile that gate leaves five of the six buttons unbuilt, so
  the configuration pokes it.** `ADDR_MOVIE_COUNT` reads ZERO here: the screen
  then has one thumbnail and a BACK button and everything else is unreachable.
  `ab.sh movies` pokes it to 3 on both sides, which builds all four thumbnails
  and the page button, then clicks the page button -- 75,897 pixels change
  across the four slots, which is the only thing that exercises the page
  arithmetic at all.

  It is a fourteenth configuration rather than an addition to `menuscreens`,
  because the title screen reaches MOVIES directly and `menuscreens` is for
  the ones it cannot.

  **Every coordinate was measured from the tree, not computed.** Panel
  67,32..573,447; the four thumbnails 144 square; BACK at 474..555 by
  264..296; the page button just above it at 214..246.

- **DELETE GAME is the one screen in the table built two different ways, and
  reading it as two layouts gets the arithmetic wrong.** `0x0044FE50`: in a
  mission there is no panel at all -- the four children go straight onto the
  screen and every rectangle carries the offset the panel would have supplied,
  `0x6C` by `0x98`. On the title screen the panel is made, the offset becomes
  zero, and the same four children go into it. The coordinates in the source
  are the SAME numbers either way; the parent and the offset move together.

  Its factory confirms the reading from the other side: in a mission it passes
  the delgame bitmap with flag 0, and on the title screen the shared backdrop
  with flag 1 -- so the constructor's `flag` goes straight through to the base
  and is what makes this one of the two constructors taking two arguments.

  `ab.sh menuscreens` pokes arm 21 last and LEAVES IT UP, so the final frame
  is the comparison: 65,332 pixels different from the frame before it, across
  exactly the panel's rectangle. All four shot slots were spoken for by then
  and this screen is the one worth the last of them. The cost is its
  destructor, which no configuration runs -- said here rather than left to be
  found.

- **ENTER NAME, and `selectmap` becomes `menuscreens`.** `0x00451AF0` is
  RECRUIT's dialog and the simplest screen that owns an edit box: a panel, one
  field writing into the SCREEN's own buffer at `0x64`, OK, CANCEL and a green
  dot. It clears that buffer before building anything, so the field always
  opens empty and the name that was there is not offered back. Its OK handler
  doubles as the field's ON-ENTER -- one address in two slots, the shape ENTER
  BATTLE NAME has too -- and the character set is installed AFTER the
  constructor, over the default, exactly as that screen does it.

  The configuration was called `selectmap` for one commit and two screens in
  the name was already wrong. It is `menuscreens` now: the screens no other
  configuration can reach, opened by poking the menu-request pair. ENTER NAME
  is reachable through RECRUIT, but driving the campaign through RECRUIT is
  what CLAUDE.md warns against -- a name that already exists is rejected in
  silence -- so the poke is the safer route here too.

  It types `Sarge` into the field and CANCELs rather than OKs, which would
  create a player directory and leave the next run driving a screen with one
  more row in it. The `alt` frame differs from the `mid` one by 107,140
  pixels, so it really is a second screen, and both sides agree on it exactly.

  **The CANCEL coordinate was measured, not computed.** Yesterday's miss cost
  a silently passing run; this one came from dumping the tree with the screen
  up: 438..519 by 246..278, so 478,262.

- **SELECT MAP, and a configuration that had to be invented to reach it.**
  `0x0044DBB0` is the campaign's level picker and the one screen whose list
  comes out of a PARSED FILE rather than the filesystem or the comm object: it
  reparses `campaign.txt` on every open -- the same `ADDR_READ_CAMPAIGN_FILE`
  SELECT PLAYER calls -- then walks the level table by id from 1, adding each
  record's display name with a `malloc`'d copy of the id beside it. The rows
  own that copy, which is why the record is constructed with its third field
  set and `RecordReset` frees it.

  The loop bound is re-read every iteration and the comparison is on `i - 1`,
  so it runs for ids 1..count. A table whose ids are not contiguous simply
  skips the gaps: a missing record is a NULL from the lookup, not an error.

  **Nothing in the game reaches it from the title screen.** The campaign goes
  through SELECT PLAYER, and the multiplayer host panel has its own picker.
  The two real routes are that panel and SINGLE PLAYER with the "Aye aye
  Captain!" cheat entered and a shift held. `ab.sh menuscreens` takes the third:
  poke the menu-request pair, which is the same pair the game itself writes
  and the technique `mpoptions` already uses.

  **It is also the only place in the suite where a list SCROLLS.** The
  campaign has more levels than the box shows, so four clicks on the down
  arrow move it -- 4,638 pixels between the two frames, across the list and
  the bar -- and that is `OnArrowDown` and the arrow bar's thumb running for
  the first time. STATUS said a day ago that they were verified by reading
  because every list this suite reached showed all of its rows at once. That
  was true of the suite, not of the game.

  **A click that lands nowhere looks exactly like one that lands.** The first
  CANCEL was at 502,297 and the button is 416..497 wide; the run passed
  anyway. The tell was the log: six messages once the click landed, five
  while it was missing.

- **`checkseams` had two bugs and the second hid the first.** The fourth
  spelling of the seam -- naming a reconstructed address at all -- went in
  report-only, with a caveat that "some are data, not calls" and about two
  hundred sites. Both were artefacts: it was scanning COMMENTS, and every
  `ADDR_` name in this tree is discussed in one. Stripping them left 21, every
  one a genuine call.

  Stripping them also exposed the older bug. The single-line `#define orig_x
  ... ADDR_Y` match never saw a macro continued with a backslash, and five of
  `commmsg.cpp`'s and one of `dplay.cpp`'s were -- six real seams that the
  gate had been green over for as long as they existed. Joining continuations
  before matching costs nothing.

  **A check nobody can read is a check nobody acts on**, and a check with a
  caveat attached is one nobody trusts. The caveat was the bug.

- **All 21 are closed and the rule is a gate.** Four groups, and each one says
  something:

  - `commmsg.cpp` reached three comm methods by address because it is in the
    FLAT half and `win32/dplay.h` names DirectPlay types. Their own signatures
    name nothing platform, so a forward declaration is enough -- which is what
    `script.cpp` already does for `PreloadSprite`. `extern "C"`, because that
    is how `dplay.h` declares them and a C++-mangled declaration links against
    nothing while looking perfectly correct.
  - `objscript.cpp` reached four functions through a SECOND set of `ADDR_`
    names for addresses that already had one -- `ADDR_OBJ_TAKES_SCRIPT` for
    `ADDR_OBJ_IS_ITEM`, and three more. Both mistakes at once, and neither
    check could see it: the alias ratchet counts names, not uses.
  - `winmain.cpp`'s thirteen-entry teardown table was plain integers, which is
    exactly the shape nothing was looking at. Eight of the thirteen are ours
    and go in by name now; the other four stay addresses, and the shape says
    which is which.
  - `air.cpp` passes a predicate to a still-original walker, which is a
    genuine function-pointer argument -- and the pointer should be OURS.

  Tested in the failing direction by putting one address back.

- **The base BUTTON's constructor and the checkbox's toggle.** `0x004542F0` is
  what every three-state button and every checkbox derives from -- the widget
  base, its own vtable, three fields cleared -- and it returns `this`, which
  `tools/checkthis.py` would now refuse to let go in as `void`. `0x00454760`
  is the toggle: the tick flips with `sete` on the old value, so it is a
  strict toggle and not a set, it plays wave 1 rather than the menus' wave 2,
  and only then does it call the caller's own handler.

  That last ordering is the point of the class. The toggle goes into
  `BUTTON_OFF_ON_LEFT` unconditionally, written by the CONSTRUCTOR, and what
  the caller asked for goes to `CHECK_OFF_ON_CHANGE` -- which is why clicking
  a plain box only ticks it while a group header also disables its group.

  Both were reached through the image by our own code until now: the checkbox
  constructor wrote `AM2_IMAGE(ADDR_CHECKBOX_TOGGLE)` into the widget and
  `ButtonConstruct` had an `orig_button_base_ctor` macro. `checkseams` caught
  the first the moment the address became reconstructed, which is exactly what
  it is for.

- **`EditCharHandler` is the whole of a text field's typing behaviour, and
  CLAUDE.md said porting it meant porting the text-field system.** It did, and
  the text-field system is ours now, so `0x0044D520` went in as the last piece
  of it. `EditTakeFocus` installs it into `g_charHandler` by name rather than
  through the image, so `winproc.cpp`'s WM_CHAR dispatch reaches our function
  directly.

  It works on the FOCUSED field rather than on an argument, and re-reads the
  global after anything that could move the focus -- the blinker restart, the
  character-set test, the change callback. With no field focused it says
  "Error: Key handler not freed", which is the game's own diagnosis of a
  handler that outlived its widget.

  Four classes of character in this order: printable `0x20..0x7F` except `^`
  and in the field's own set; backspace; RETURN, which fires the field's
  on-enter; TAB, swallowed. Anything else is wave 3. The printable arm has two
  refusals of its own -- the text would be WIDER than the field, or it is at
  the field's maximum -- and the width test runs BEFORE the character is
  added, on the text as it stands plus a 12-pixel margin, so the field stops
  one character early rather than overflowing and backing out.

- **`ab.sh multi` typed "Zulu" and reached one arm of four.** It now types
  `Zulu^Battle Royale With Extra Cheese`, and the buffer read back out of the
  running game says what happened: `"ZuluBattle Royale With "`. The `^` is
  gone, so the excluded-character path ran; the string stops at "With ", so
  the WIDTH refusal ran. Two arms that no configuration had ever reached,
  confirmed by reading the field rather than by trusting the click.

  Backspace and RETURN are still unexercised: the control socket's `type`
  sends the characters of a line, and neither is one.

  **And the coverage figure did not move for it**, which is the split-point
  problem again: `0x0044D520` is not in `tools/merges.py`'s referenced-starts
  set, because its only reference is the `push imm32` inside `EditTakeFocus`
  and that instruction's operand is not where the scan looks. So 336 bytes of
  reconstruction are real and uncounted. The percentage is a lower bound in
  both directions now -- too high where an entry cannot be split, too low
  where a function's only reference is an unaligned operand.

- **The "session" pair is a three-field RECORD, and both names came from one
  call site.** `0x00453910` and `0x00453940` were `ADDR_SESSION_CTOR` and
  `ADDR_SESSION_RESET` because the multiplayer session object is what the site
  that named them passes. Their bodies are `{count, rows, ownsRows}` -- the
  same shape a list box's row array is, which `orig.h` already noted in a
  different place and under a different name. Renamed to `ADDR_RECORD_CTOR`
  and `ADDR_RECORD_RESET`, which is what `widget.cpp` has been calling them
  since `RecordCtor` was written.

  `RecordReset` frees every row's owned dword, then the array, then clears the
  count -- and it frees the array whether or not the flag is set and whether
  or not the count is zero, leaning on `free(NULL)` rather than testing.
  `CommEnumSessions` calls it by name now instead of through the image.

- **`SelectPlayerRow` is why the three buttons beside it never look at the
  list.** `0x004512A0` is what a list box calls when its selection moves, and
  the dispatch is `callback(list, rows, selected)` -- found by looking for a
  register loaded from `+0x68` and called a few instructions later, since the
  offset is never in the `call` itself. This one ignores the list, which is
  why its arguments start at the second stack slot and read as odd until the
  dispatch is known.

  All it does is copy the chosen name into `ADDR_GAMEPROC_BLOCK`. By the time
  SELECT, DELETE or RECRUIT is pressed the name is already there, which is
  exactly what those three handlers assume.

- **SELECT PLAYER's three buttons, DELETE PLAYER's CANCEL, and the REPLAY
  prompt's OK.** `0x00451300`, `0x00451330`, `0x00451380`, `0x00450A10` and
  `0x0044F1B0`. What three of them test is the player NAME in
  `ADDR_GAMEPROC_BLOCK` -- measured with `strlen` and refused with wave 3, the
  game's "no". SELECT does not look at the list at all: clicking a row has
  already copied the name in.

  SELECT stores the level id as the literal 1 rather than the record's own
  first field, which is where it differs from the Boot Camp button doing the
  same lookup two screens away.

- **The REPLAY prompt's OK is a SECOND writer of the load flag, and open item
  2 already knew the first.** `0x0044F1B0` sets `0x00511DD8` when the second
  name is present and then asks for state 2 -- the same global the GAME SELECT
  PANEL's LOAD arm sets, and the same one mission start reads at `0x00425360`.
  So this is not the missing route to `LoadGame`; the puzzle stays exactly
  where item 2 left it, which is that the flag is set, read as set at
  `0x00425360`, and read as 0 again by `0x004255CB`.

  What it does add is a name -- `ADDR_LOAD_PENDING`, which that item has been
  writing out as a bare address -- and a second way to reach the path, which
  matters if the "entered twice" reading is right: the two writers arrive from
  different screens.

  `0x0051232C` comes with it: both arms of the handler set it, and the level
  teardown turns it into the "Attempt# %d" line, so it is `ADDR_MISSION_RETRY`
  with `ADDR_ATTEMPT_COUNT` beside it.

- **`ab.sh campaign` clicks DELETE and then CANCEL, which deletes nothing and
  covers two handlers no configuration reached.** The four buttons are 39
  apart at x 416..497 and the confirm dialog puts OK at y 208..240 and CANCEL
  at 249..281, so 265 is CANCEL with sixteen pixels either side -- worth being
  exact, since a miss deletes the campaign player and the next run drives a
  screen that is not there. Verified by dumping the tree with the dialog up
  before wiring it in.

- **A counter that MOVES on a handler only our own code installs is the tell
  that the install went through the image.** `OnDeletePlayer` read 1 on the
  probe, and it should not have been able to: the only caller is the button
  widget, whose handler our own constructor writes. It read 1 because the
  constructor was still passing the ADDRESS, so the call went out to the
  detour and back. Closing that seam takes the counter to 0.

  All of `widget.cpp` is closed now and the address-taking `MakeButton` is
  gone -- every site passes a pointer, `kImageHandler(ADDR_X)` where the
  handler is still the original's and the function itself where it is not.
  29 by-address sites remain elsewhere in `src/game`.

- **The four arrows are two pairs on two different classes, and only the
  shapes rhyme.** UP and DOWN (`0x004557F0`, `0x004558B0`) belong to the ARROW
  BAR beside a list and move the list's first drawn row; LEFT and RIGHT
  (`0x00455ED0`, `0x00455F60`) belong to the SCROLL BAR, move its own
  position, and then fire its `onChange`.

  So an arrow click on the AUDIO dialog is how `OnVolumeEffects` is reached,
  and `ab.sh audiovol` was clicking one all along without anyone saying so:
  x 355 is inside the right arrow, which spans 351..360, not the trough.

  **The guard skips the MOVE and not the NOTIFICATION.** Both arms of the
  scroll bar's test fall into the same tail, so holding an arrow against the
  end of the track keeps calling `onChange` with an unchanged position.
  Written as an `if` around the move rather than an early return, because an
  early return is the wrong shape and would be invisible until something
  depended on the callback.

  `audiovol` now also clicks the LEFT arrow six times, enough to run the
  position off the bottom, which is the only way to reach that arm. The two
  bars move 481 pixels between the dlg and mid frames and the two sides agree.

  **UP and DOWN were verified by reading for one day.** They need a list with
  more rows than fit, and every list the suite reached -- COMM CHANNEL, SELECT
  PLAYER, DIFFICULTY -- showed all of its rows at once. That was a fact about
  the SUITE: SELECT MAP has more levels than its box shows, and `ab.sh
  menuscreens` now scrolls it. The guard is still what saves the short lists,
  since `count - visible` would be a division by zero otherwise.

- **`0x00455C10` is not `WidgetRepaint` and is a near-twin of it.** Both walk
  up to the nearest ancestor owning a sprite and paint through it; this one
  omits the `0x48` test and does not clear `0x44`, and it is `thiscall` with
  `ret 0x10` -- a clip rectangle it accepts and ignores. All four arrows end
  in it. Named `ADDR_REPAINT_ANCESTOR` from its body, and left original.

- **CONTROLS' OK and DEFAULT, and the key table has two columns but one is
  never written.** `0x00451150` walks the dialog's twenty-one rows and stores
  each row's key into `ADDR_KEY_BINDINGS`, which is pairs of bytes -- and only
  the first of each pair, which is what makes the stride 2 against an array of
  row pointers with stride 4. Then it saves, plays a sound, and calls CANCEL:
  the original literally calls `0x00451100` as its last instruction, so OK is
  "apply, then leave the way CANCEL does".

  `0x004511A0` is DEFAULT: one scancode per row from `ADDR_KEY_DEFAULTS`, each
  turned into a table INDEX by `KeyNameIndexOf` -- the form a row stores -- the
  label taking that entry's name, and the row repainted through its own slot 1.
  It does not save and does not leave; the key table is untouched until OK.

  `ab.sh controls` clicks DEFAULT, shoots, then OK, where it used to click
  CANCEL. Measured: the frame moves 760 pixels across the key rows when
  DEFAULT lands, so the handler is being seen rather than merely called.

- **`checkseams` has a fourth blind shape, and it is the one that let a
  reconstructed handler be installed by address.** The first three spellings
  it knows are `#define orig_x ... ADDR_Y`, `callN(ADDR_Y)` and a cast around
  `AM2_IMAGE(ADDR_Y)`. The fourth is neither: `MakeButton(..., ADDR_ON_MENU_BACK)`
  passes an address to a helper that applies `AM2_IMAGE` to the parameter, so
  the call site names a reconstructed function and nothing on the line looks
  like a call at all.

  `tools/checkseams.py --by-address` reports every use of a reconstructed
  address in `src/game`. It is REPORT ONLY and that is deliberate: it found 48
  and not all of them are calls. Some are DATA and correct as data -- a list's
  "no callback" field is written `AM2_IMAGE(ADDR_NULL_STUB)` and the game
  compares pointers against that exact address, so pointing it at ours would
  break the comparison. Sorting the calls from the sentinels is what would
  turn it into a gate.

  All nineteen in `widget.cpp` are closed -- `MakeButton`, `MakeWideButton`,
  `MakeVolumeBar` and `ConfirmDialogBuild` take a pointer now, with the
  address form kept beside it as `kImageHandler()` for the handlers that are
  still the original's. 29 remain elsewhere: teardown tables, script
  callbacks, and the sentinels.

- **`windowed` fails about half the time and it is the machine, not the
  code.** Wine hands this prefix a lockable primary on some runs and not
  others, so one side's client area can be flat while the other's is painted,
  which compares as 195,000 pixels of nothing. `ab.sh` now detects exactly
  that -- one side's client area a single colour, the other's not -- prints
  "one side's client area never painted" and does not compare. Both flat and
  both painted still compare normally, at 0 and at 2 to 10.

  It is scoped to `windowed` on purpose. A general "skip when the frames look
  too different" rule is how a suite stops catching things.

- **The OPTIONS dialogs' OK and CANCEL, and a saved file that made three of
  them untestable.** Five more: CONTROLS' CANCEL (`0x00451100`), AUDIO's
  CANCEL and OK (`0x0044F8B0`, `0x0044F930`), the three-volume apply beneath
  the second (`0x0044F860`), and DIFFICULTY's OK (`0x0044EA80`).

  **Every dialog that opens from two places ends in two ways.** In a mission
  the OPTIONS screen is an overlay, so the exit writes `MENU_MODE_OPTIONS` and
  marks the primary dirty; on the title screen it is menu request 14.
  `ADDR_GAME_STATE == 2` is the test and three of these carry a copy of it.
  DIFFICULTY's OK does NOT -- it always asks for 14 -- which is either a bug
  in the original or a screen that cannot be opened from play. Reproduced
  either way.

  Two functions beside each other solve the same problem differently and both
  are kept: AUDIO's CANCEL walks `parent` to the top to find the screen, and
  AUDIO's OK branches on the game state to take one parent or two, because the
  overlay has a level less nesting. CANCEL then tests the walk's result for
  null, which cannot happen -- it starts at the widget itself.

- **`ab.sh` saves and restores `Options.cfg` around every side, and that is
  what makes an OK clickable at all.** `audiovol` deliberately clicked CANCEL,
  with a comment saying OK writes the volume out and would leave the next run
  starting somewhere else. True, and worse than it sounds: the sides run in
  order, so orig would write the file and recon would then open the dialog on
  what ORIG chose. One `cp` either side of the run removes the whole problem.

  It buys three functions that were otherwise verified by reading, and it is
  measured rather than assumed -- with the restore disabled, one drive of the
  audio dialog changes bytes 5 and 6 of `Options.cfg`, so the OK really does
  reach the writer. `difficulty` now picks the middle row and confirms it for
  the same reason.

  The writer itself (`0x0044CFA0`) stays original: it is CRT file I/O, which
  this port replaces wholesale rather than function by function.

- **The OPTIONS menu's buttons and the AUDIO dialog's three bars.** Eight more
  handlers: BACK, CONTROLS, DIFFICULTY and AUDIO (`0x0044E670`, `0x0044FD40`,
  `0x0044FD70`, `0x0044FDA0`), CONFIRM GAME EXIT's OK (`0x0044EE30`), and the
  three volume bars' onChange (`0x0044F2A0`, `0x0044F2E0`, `0x0044F320`).

  The first four really are the same four instructions with one immediate
  changed, so they share a helper -- checked against each other first, which
  is the habit the title screen's `focusedChild` cost earlier today. QUIT's OK
  asks for no screen at all: it goes straight to state 4, which is the only
  handler in the family that ends the process.

  **A volume bar's arithmetic is `(pos - 20) * 100`, with silence a special
  case rather than the end of the ramp.** Twenty-one positions, DirectSound
  wanting hundredths of a decibel of attenuation, so the range is -2000..0 --
  and -2000 is then replaced by `DSBVOLUME_MIN`. The original computes the
  value and compares against the literal -2000, so the test is on the result
  and not on `pos == 0`.

- **`audiovol` dragged one bar of three, and now drags all three with a sound
  device attached.** They sit 69 pixels apart and their handlers differ in
  what they do with the answer -- a sample, the music stream's volume, a
  random voice line -- so two thirds of the family were uncompared. Before and
  after on the original side differ by 324 pixels spanning y 173..340, which
  is all three thumbs; the two sides then agree to 35.

  **It is also the only place in the project that reaches `SpeakLine`**, which
  STATUS has listed as unexercised for as long as it has been reconstructed.
  Everywhere else it is a unit reacting to something in a live mission.
  Measured rather than claimed: the game's own LCG at `0x0048CC1C` advances
  four steps across one click on the voice bar and none across a click on the
  effects bar. One of the four is the handler's own `rand() % 30` and the rest
  are past SpeakLine's owner check.

  Its counter still reads 0 and will keep doing so -- our handler calls it by
  name, which is the blind spot `tools/blindspots.py` exists for. The LCG is
  the probe that resolves it.

- **The seam that reaches our own code through the image has a third shape,
  and `checkseams` sees none of it.** A menu handler is installed by address
  and the helper applies `AM2_IMAGE` to the parameter, so nothing in the text
  names an address: `MakeButton(..., ADDR_ON_MENU_BACK)` and
  `MakeVolumeBar(..., ADDR_ON_VOLUME_EFFECTS)` both looked clean while
  routing through a detour into us. The check caught only the two escape slots
  beside them, which spell the macro out.

  `MakeButtonFn` and a pointer-taking `MakeVolumeBar` are the fix, with the
  address forms kept for the dozens of handlers that are still the original's.
  **The tool resolves macros, not dataflow** -- worth remembering before
  reading a green `checkseams` as "no seams".

- **`windowed`'s pixel-perfect zero was a claim about a frame that never got
  painted.** The client area used to stay black -- CLAUDE.md said Wine hands
  this prefix no lockable primary -- and a black rectangle against a black
  rectangle is exactly 0, which reads as the strongest line in the A/B table.
  It is not black here now. It paints, mostly white, and four shots two
  seconds apart differ by 4 pixels each in one 10x10 box at (325,232), so
  something in it blinks.

  Not caused by anything in this session: `git stash push -- src/` and rebuild
  reproduces it at HEAD. The budget is 50 now, the wait is 60 rather than 30 --
  at 30 the shot often lands while one side is still black, which compares as
  195,785 pixels of nothing -- and both numbers are in `ab.sh` with the
  measurement beside them.

  Two lessons, and the first is the uncomfortable one. **An exact zero can mean
  the test is not looking at anything**, and this one was quoted as evidence
  for a year. The second is that the frame is worth MORE now than when it was
  perfect: a painted windowed frame compares the whole menu render path in a
  second configuration, where a black one compared nothing at all.

- **The title screen's seven buttons, and the boundary is down to one
  function.** `0x0044D2E0` to `0x0044D4F0`, none over 160 bytes: a menu sound,
  sometimes a global, then a menu request whose code is the arm number in
  `docs/screens.md`. MOVIES asks for 13, OPTIONS for 14, QUIT for 17,
  MULTI-PLAYER for 6. CREDITS asks for no screen at all -- it sets the
  game-over reason to 4 and requests state 0, so the credits are the
  end-of-game sequence played from the title.

  Two of them hold CD checks, which is why this closes the boundary rather
  than merely adding seven small functions. `docs/boundary.md` read **3
  functions and 6 sites** for a long time and now reads **1 and 2**: the last
  is `0x0042F290`, and like these it is a `MessageBoxA` and its
  `GetActiveWindow` behind a check this build jumps past. Both new ones went
  in through `RequireGameCD()`, which `cdcheck.h` already had.

  **The two CD arms are not the same, and the difference is not tidiness.**
  SINGLE PLAYER's sets the menu request AND the pending flag to 1; BOOT CAMP's
  sets only the request, so nothing consumes it. Reproduced as written.

- **"Aye aye Captain!" is a level select, and the flag says so in one place.**
  `0x004FCF98` is written by the cheat handler at `0x00417CAA` and read by
  exactly one instruction in the image: SINGLE PLAYER, which with the flag set
  and either SHIFT held asks for SELECT MAP (arm 2) instead of SELECT PLAYER
  (arm 3). A global with one reader is worth following to it -- the name comes
  free.

- **`ADDR_ON_DIALOG_CANCEL` was a name from a call site, the fourth.**
  `0x0044D490` is the DIFFICULTY dialog's CANCEL and its escape action, which
  is where the name came from; its body plays a sound and asks for menu
  request 14, the OPTIONS menu. It is `ADDR_ON_OPTIONS_MENU` now, and the two
  spellings of it in `widget.cpp` are the reason the rename mattered.

- **`checkseams` caught one of the two ways a reconstructed handler reaches
  itself through the image, and could not see the other.** A button handler is
  installed by address, and `MakeButton` turns that address into a pointer
  with `AM2_IMAGE` -- correct while the handler is the original's, and a lie
  once it is ours. The check found the DIFFICULTY dialog's escape slot, where
  the address is written as `AM2_IMAGE(ADDR_ON_OPTIONS_MENU)` and resolves
  statically. It could NOT find the title screen's table, where the same call
  reads `AM2_IMAGE(b->handler)` and the handler is a struct field: nothing in
  the text names an address, so nothing fires.

  `MakeButtonFn` takes the pointer instead and `MakeButton` is a wrapper over
  it, so the seven that are ours are installed by name and the dozens that are
  still the original's keep going through the image. Worth saying plainly what
  the tool sees: it resolves `ADDR_` macros in the text, so a seam that
  reaches the image through a variable is invisible to it.

- **The TITLE SCREEN, and the binary patch that lives inside it.** `0x0044D730`
  is arm 1 of the menu table and the one arm that is not a factory: it builds
  its seven buttons inline rather than calling a constructor. Before any of
  them it chdirs to `shared`, calls `CommClose` and `CommDropDirectPlay`, and
  clears the session role and the two saved names -- so coming back to the
  title is how a multiplayer session is torn down, and `quit`'s final frame is
  this function's output compared at zero.

  **It also holds the byte that removes MULTI-PLAYER.** `0x0044D8FE` is an
  ordinary `je` on the allocation in the retail compile -- the same null check
  the other six buttons have -- and an `EB` here, so that one row is skipped
  unconditionally (`docs/binarypatches.md`). A reconstruction cannot honour a
  patch inside the function it replaces, so the switch had to stop being only
  a byte: `restore.c` still patches it, because the A/B's `orig` side runs the
  original builder and needs it, and it now also exposes
  `restore_multiplayer()`, which our version consults. Both read the same
  variable, which is what keeps the two sides of a run agreeing.

  **One line of the seven blocks is not shared, and dropping it moved the A/B
  three screens.** After the FIRST button goes in -- and only then -- the
  original reloads the screen from its global and writes the button into
  `0x34`, the focused child. Written as a loop, that line disappears; the title
  screen then opens with nothing focused, and `multi` ended on COMM CHANNEL
  SELECT where the original was two screens further on at ENTER BATTLE NAME,
  131,676 pixels apart, with `quit` losing its whole comm teardown.

  This is the OPTIONS menu's defect exactly, in a function written four days
  later: compressing seven near-identical blocks into a table again dropped
  the one instruction only block zero carries. Knowing the failure did not
  prevent it. What did catch it, again, was running the configuration that
  covers what was touched -- and here the pixels named it before the tree
  did, because the tree was dumped on a screen the two sides no longer
  shared.

- **An i386 MSVC constructor returns `this` in eax, and a reconstruction that
  drops it had been killing the multiplayer path for four days.** `RecordCtor`
  (`0x00453910`) sets three fields and its body was byte-for-byte right; what
  it did not do was return. Declared `void`, it left the `value` argument in
  eax, and the caller at `0x00451473` stores that straight into the dialog's
  `0x0064` -- so the SELECT PLAYER and COMM CHANNEL lists became the pointer
  `1`, and the next `ListAdd` took the process down.

  Nothing static could see it. `make check` was green throughout, the body
  passes any reading, and `tools/checkdetour.py` says the patch site is sound.
  The only witness was `tools/ab.sh multi`, which had gone from 8 widget nodes
  to none and 291,000 differing pixels at `19282a4` and stayed that way through
  eleven commits, because nothing re-ran it. **Run the configuration that
  covers what you just touched, not the one that is quickest.**

  Found by bisecting `19282a4..HEAD` on one question -- does clicking
  MULTI-PLAYER leave the process alive -- eight builds, and then by disabling
  the commit's eight patches two at a time. The trace's last line was
  `ListAdd#1("Internet TCP/IP Connection For DirectPlay", ...)`, which named
  the list but not the pointer.

  The tell is exact and cheap: MSVC opens such a function with `mov eax, ecx`
  purely so the value survives to the `ret`. `tools/checkthis.py` resolves
  every patched address, reads the original's first two bytes, and fails if a
  function that opens `8B C1` is reconstructed as `void`. It is in `make
  check`, and tested in the failing direction by putting `void` back.

  It found a second one immediately. `InitPtrList` (`0x0042A660`) has the same
  shape and the same omission, and its caller at `0x0040A628` stores the result
  too -- `eb 02 / 33 c0 / 89 06`, the identical `jmp` past an `xor eax,eax`.
  Nothing has reached that caller yet, so this one was found before it cost
  anything.

- **The whole MULTIPLAYER OPTIONS screen is reconstructed, and it is
  DECLARED rather than built.** `OptionsDefaults` (`0x00432710`),
  `OptionsApply` (`0x004327A0`, which names itself "Options changed by host."),
  `OptionsRequest` (`0x00432830`), `OptionsSyncGroup` (`0x00432870`) and the
  two aliases in front of them, `MpDialogDestruct` (`0x004326F0`) and
  `OptionsUpdate` (`0x00432700`).

  A 43-record table at `0x004865B8`, 36 bytes each, is the entire screen: per
  checkbox an x, a y, a label, the bit it owns, which of two masks that bit is
  in, and -- for the five group headers -- the range of boxes it commands. The
  columns and the mask choice agree exactly: records 0..21 are the left column
  and `ADDR_GAME_OVER_FLAGS`, 22..42 the right and `ADDR_GAME_SETTING_22C`.

  **Its end is not the literal the original compares against, and taking it as
  one froze the game.** The loop walks with a cursor 0x18 bytes INTO each
  record, so the bound in the image, `0x00486BDC`, is 0x18 past the last
  record's base. Read as a record bound it runs one record too far and lands in
  the label strings -- `"Heavy MG Pillbox"` decoded as a widget index, which is
  a wild pointer. The first click of DEFAULT stopped the frame loop dead;
  `OptionsUpdate` froze at a fixed count and nothing else logged. The table is
  43 records, `0x004865B8..0x00486BC4`.

- **`tools/ab.sh mpoptions` is the configuration that compares it**, and it
  needed a new harness command to exist at all. The screen is reached through a
  DirectPlay session that will not open on this machine, so `poke` -- a
  one-dword write, symmetric with the `dump` that was already there -- writes
  the menu-request pair that the game's own ESCAPE handler writes. Those are
  the GAME's globals, so the same three commands drive both sides and
  `AM2_NOPATCH=1` takes them unchanged; that is what makes it an A/B rather
  than a demonstration.

  One more poke earns its keep: `comm+0x3D8` is the host flag, and without it
  the panel is read-only with CANCEL alone. Set, OK and DEFAULT appear and the
  three interesting functions become clickable. The run ends on the lobby with
  "Options changed by host." in the comms panel, which is the apply's own last
  line and about 360 pixels of menu text -- comfortably over the budget of 200,
  so an apply that stopped short would be caught.

  Counters from one traced run: `OptionsUpdate` 26,355, `OptionsApply` 1,
  `OptionsDefaults` 1, `OptionsSyncGroup` 1, `OptionsRequest` 1,
  `MpDialogDestruct` 2. `Announce` reads 0, which is the usual blind spot --
  `OptionsApply` calls it directly.

- **`mpoptions` failed on its first run and found a real defect, and it was
  ours after all -- just not from this work.** `MakeBitmap` reserved the first
  ten palette entries when `BMP_FLAG_RESERVE10` was CLEAR. The original
  reserves them when it is SET.

  The symptom was 918 pixels in the lobby's map preview, x 338..523 /
  y 272..457 and nothing else on the frame. Four colour pairs, 888 of them the
  original's `(0,0,128)` against our `(0,128,128)`, the rest `(128,0,128)` and
  `(192,192,192)` -- VGA entries 4, 6, 5 and 7, all inside the ten Windows
  reserves. Both sides were choosing from that block; ours was allowed to and
  the original's was not.

  How the sense got lost is worth keeping. The original holds it in ONE
  register and reads it both ways round: `ebp` is `(flags & 0x80) == 0`, the
  branch at `0x0041BEDE` jumps PAST the identity fill when `ebp` is non-zero,
  and the `or al, 0x10` at `0x0041BFBD` tests the same register the other way.
  Our transcription got the second right and the first backwards, so the two
  halves of one flag disagreed. It is now written from the FLAG rather than
  from the register, with the `?:` arms swapped so that half stays identical.
  **When the original reuses a register for a predicate, decide what the flag
  means once and write every use from that.**

  Measured, not argued. Taking the lobby frame directly on both sides went
  from 918 to **50**, which is the cursor; the same screen's own animation
  measures at most 54 over four seconds. Then the full configuration: 918 to
  **0**, on all three of its frames, with 42 widget nodes and 35 log messages
  identical. `bootcamp` stays at 22 and `windowed` stays pixel-perfect, which
  is what says the inversion cost nothing elsewhere -- every sprite in the game
  goes through `MakeBitmap`.

  The sibling site is right, which is the contrast worth keeping: the tileset
  loader in `mapdraw.cpp` reserves when the global at `0x00511CC8` is non-zero,
  and `0x0042C215` really does skip the fill on zero. Two remaps of the same
  shape, two different conditions, and only one of them inverted.

  It survived this long because nothing had ever reached that screen. Boot Camp
  is clean either way, which says the flag is clear for everything it loads;
  the preview is the first bitmap in the project that sets it. Two things
  follow. `NearestPalIndex`'s `from` guard, which CLAUDE.md records as never
  having been discriminated by any configuration, IS discriminated here -- it
  was doing its job and being handed the wrong threshold. And a whole-frame A/B
  that never visits a screen says nothing about it: this defect sat behind
  every green run in the suite.

- **The first three menu screen factories are reconstructed**: `OpenMpHost`
  (`0x004317C0`), `OpenMpJoin` (`0x00433480`) and `OpenMpOptions`
  (`0x00432910`). One shape -- close the current screen, allocate, construct,
  store the constructor's return -- and the store is the RecordCtor lesson
  again, which is why this family was worth taking next.

  The host and join panels are ONE class: both allocate 0x278 and call the
  same constructor at `0x00430530`, differing only in backdrop and in what
  they write to `ADDR_MP_SESSION` -- 1 and 2. Driving menu request 9 puts up
  "MULTIPLAYER JOIN PANEL" where 7 puts up "MULTIPLAYER HOST PANEL", from the
  same vtable. That confirms by running what `orig.h`'s comment on that global
  had worked out by reading; what stays open is the title screen also writing
  2, which host-versus-client still does not explain.

  They went into `widget.cpp` rather than a `screens.cpp` of their own, and
  the ratchet is why. A separate module names no Win32 or COM type at all, so
  `tools/checksplit.py` refused it -- correctly. The flat-half alternative
  would have meant writing the vtable call against `void **` instead of
  `AM2_WidgetDeleteFn`, which is exactly the private signature that hid the
  `PlaySoundAt` defect. The rule pointed at the right answer both times.

  Counters from one driven run: `OpenMpHost` 1, `OpenMpOptions` 1,
  `OpenMpJoin` 1, each with the right screen on the frame.

- **`RefreshScreen` is what a menu screen opened DURING a mission calls, and
  that answers a standing open item.** CLAUDE.md lists it as unexercised with
  seven callers and says "whatever forces an out-of-band repaint is somewhere
  further in". It is the screen factories: four of them -- AUDIO, DELETE GAME,
  LOAD GAME and the two-branch pair beside them -- open with
  `cmp [ADDR_GAME_STATE], 2`, and the state-2 arm calls `RefreshScreen` before
  it allocates.

  So the two arms are the same screen in two contexts. In a mission it gets
  its own backdrop and a flag of 0, and the frame under it has to be repainted
  because a mission is not a menu; on the title screen it gets the shared
  backdrop and a flag of 1 and no repaint is needed. That is also why nothing
  in the suite has ever run it: every configuration that opens a menu screen
  does so from the title.

  **The obvious probe does not work, and that is worth knowing before anyone
  else spends an hour on it.** Driving Boot Camp to live play, confirming
  `ADDR_GAME_STATE` reads 2, and then poking the menu-request pair leaves
  everything untouched: `RefreshScreen` 0, `OpenAudioOptions` 0, no dialog,
  and the state still 2. Nothing in ordinary state-2 play consumes that pair
  -- the consumer is `0x00425EE0`, which is the in-mission ESCAPE arm, and
  that is sub-state 34 where ordinary play sits in 33.

  So the state-2 arms of those four factories are verified by reading, and
  what is missing is not a way to poke but the CONTEXT in which the menu
  dispatcher runs with the game state at 2. Find that and `RefreshScreen`
  runs for the first time in this project.

- **The `args` column in `docs/screens.md` earned itself immediately.** These
  constructors are thiscall, so the CALLEE pops: reconstructing a two-argument
  one with a single argument corrupts the stack rather than merely painting
  the wrong screen. Sixteen of the twenty-one take one argument and are
  reconstructed; the remaining five take two, and they are exactly the five
  that are not. That correspondence was not designed -- the batch was chosen
  by "has no branch and no extra call" and the argument counts agreed
  afterwards.

- **`tools/checkseams.py` had two blind spots and the second one hid eight
  live seams.** It knew two spellings of "reach our own code through the
  image": an `orig_` macro on one line, and `callN(ADDR_X)` at a call site. It
  did not know the third -- a cast around `AM2_IMAGE(ADDR_X)` written inline at
  the point of use, which is the same function pointer without a name on it.
  Nor did it know a two-LINE `#define orig_x \` ... `AM2_IMAGE(ADDR_Y)`,
  because its regex wanted both on one line.

  Found because a seventh instance appeared and only that one was reported --
  it happened to have been given a name. Teaching the check the third spelling
  found **eight more, none of them from this work**: `ListRemoveAt` in
  `army.cpp` and `event.cpp`, `ArmyMessageSend` in `event.cpp`, and five
  comparator pointers handed to `bsearch` in `defparse.cpp`. Every one was a
  call into our own reconstruction routed through a detour. All closed.

  A ratchet only guards the spellings it knows. When one fires, ask what else
  would have looked the same and not fired.

- **The ARROW BAR is reconstructed** (`0x00455970`, 480 B) -- the vertical bar
  the connection list and the player list carry. It is the horizontal scroll
  bar transposed: arrows 0x13 by 9 where that one's are 9 by 0x13, the second
  at top + height - 9 rather than left + width - 9, and the same
  two-arrows-built-from-buttons trick.

  **Its nine argument slots were worked out by tracking `esp` in a script, not
  by counting pushes.** With nine arguments and five calls in the middle --
  one of which pops 0x28 -- the same `[esp + 0x58]` means a different argument
  at every point, and three slots here are read twice at different depths. A
  dozen lines of Python settled in a minute what an hour of care would have
  got wrong somewhere. Worth reaching for whenever an argument list is long
  enough that the offsets stop being obvious.

- **And the byte figure did not move, which is worth knowing about the
  figure.** `docs/functions.tsv` lists `0x00455970` as one 512-byte entry, and
  `ArrowDelete` at `0x00455B50` sits inside it -- so patching that deleting
  destructor months ago had already credited the whole span, arrow-bar
  constructor included. Writing 480 bytes of real code moved the total by
  zero.

  **And the cause is not what I said it was.** `reconstructed.py` does split
  merged entries -- it has used `merges.real_functions` all along. The split
  point simply cannot be FOUND: `ret 0x24` sits at `0x00455B4D` and `push esi`
  at `0x00455B50` with **no padding between them at all**, and both of
  `merges.py`'s rules require padding. A function MSVC did not align is
  invisible to the splitter.

  Loosening the rule to "a referenced address straight after a `ret`" would
  find this one and would also invent split points at switch arms, which are
  referenced through jump tables and can follow an early-exit `ret`. That
  trades a number that is conservative for one that might be wrong, which is
  the opposite of what CLAUDE.md asks for -- it says plainly not to rewrite
  `functions.tsv` from the naive scan. The rule stays.

  So the percentage is an upper bound by an unmeasured amount, and has been
  quoted all session as though it were exact. That much stands; only the
  explanation was wrong.

- **The TYPEWRITER is reconstructed, and writing it found a `void` that
  should have been a return** -- the RecordCtor lesson a third time, in a
  function reconstructed months ago.

  `TextExtent` (`0x004468A0`) accumulates the width into eax and its
  null-`out` branch falls straight through to the `ret`, so eax IS the answer.
  Ours was declared `void`. Nothing could see it: every earlier caller passes
  a real `out` and ignores the return. The typewriter's word-wrap is the only
  caller in the image that passes NULL and uses eax, so the defect became
  reachable and visible in the same commit.

  **`tools/checkthis.py` does not catch this shape** -- it looks for
  `mov eax, ecx` at entry, which is the constructor idiom. A function that
  merely leaves its answer in eax has no such tell, and the only reliable
  signal is a call site that consumes the result.

- **The TYPEWRITER constructor: the word-wrap IS the constructor.**
  `0x004566F0`, 627 B, `ret 0x14` -- rectangle then message. It is not a
  constructor that stores things: it **word-wraps the message** into the
  0x400-byte buffer at 0x0058 and the wrapping is the function.

  The loop takes the next space with `strchr`, copies the candidate line,
  measures it with the text-extent helper at `0x004468A0`, and compares
  against `rect.right - rect.left - 12`. Over-width, it commits the LAST
  fitting run and appends the newline at `0x0048BD8C`; otherwise it remembers
  the fit and takes the next space. Two loop-carried offsets, two back-edges
  into the same head, and every `strlen`/`strcat` inlined as `repne scasb`
  plus `rep movsd`.

  Two behaviours fell out that are worth stating rather than rediscovering. A
  word wider than the line commits an EMPTY run and then measures the same
  word again against an empty line, so it ends up alone on its line and
  overflows rather than being broken. And the tail after the last space is
  measured once more, so a final word too wide for what is left also gets its
  own line.

- **The SCROLL BAR constructor is reconstructed** (`0x00455FF0`, 453 B) and it
  confirms by running what `widget.h` had worked out by reading: **the arrows
  have no constructor.** Each one is a BUTTON with `VTABLE_ARROW` stamped over
  the button's vtable afterwards.

  They are also the one place the button's null-bitmap branch is taken. Each
  arrow goes in with a NULL first bitmap, which sets 0x0048, which
  `WidgetRepaint` reads as "defer to an ancestor" -- so an arrow has no
  backdrop of its own and the bar behind it is what gets redrawn. A branch
  that looked defensive when the button went in has exactly two callers and
  they are both here.

  And the RANGE is a literal **twenty**, which is the number the AUDIO dialog
  divides a position by and which matches `(volume + 2000) / 100` landing in
  0..20. Two independent places agreeing on twenty is better evidence than
  either alone.

- **The CHECKBOX constructor is reconstructed, and writing it found a defect
  in code that had already passed an A/B twice.** `0x00454640`, 255 B.

  **Its left-click action is the constructor's, not the caller's.**
  `ADDR_CHECKBOX_TOGGLE` goes into `BUTTON_OFF_ON_LEFT` unconditionally and
  the caller's handler goes to a separate change slot -- which is why clicking
  a plain box only ticks it while a group header also disables its group.
  Both run the same `OptionsSyncGroup`; what differs is the RECORD INDEX the
  constructor stored.

  And that index is the ninth argument, which `MpOptionsConstruct` was passing
  as a literal **0** -- I had read the original's `push ebp` as a constant
  when ebp is the loop counter. Every checkbox got group 0.

  **`mpoptions` passed anyway, twice, at 42 nodes and 0 pixels**, because it
  clicks POWER-UPS -- and POWER-UPS *is* record 0, so the wrong index synced
  the right group by accident. The configuration now clicks MISCELLANEOUS,
  record 17, where only the right index reaches the right group. A test that
  can only pass is worth as little as one that cannot fail, and this one could
  only pass for one specific input.

- **The LIST BOX constructor is reconstructed** (`0x00454F90`, 209 B), and it
  is where the row height finally appears. **A row is FOURTEEN pixels tall**,
  and that number is written nowhere in the image: it comes out of a
  magic-number division, `LIST_OFF_VISIBLE = (height - 4) / 14`, spelled
  `imul 0x92492493` then `sar 3`.

  **The constant alone does not say the divisor.** `0x92492493` serves 7, 14
  and 28; what picks between them is the SHIFT. I read the constant, wrote 7,
  and every list drew twice as many rows as it had room for -- seven map names
  on a lobby that shows four. Caught by `mpoptions` on the very next run, and
  only because that run also changed which header it clicks. Recognising a
  magic number is half of reading one.

  Its hot row starts at **-1** -- nothing under the pointer -- and becomes 0
  only if the rows it was handed are non-empty, testing the pointer and then
  the count. The selected row is 0 either way. So an empty connection list
  opens with nothing hot and a populated one opens with the first row hot,
  which is what DIFFICULTY's green bar and the player list's highlight are.

- **The EDIT BOX constructor is reconstructed** (`0x00454C10`, 137 B) and it
  has **thirteen stack arguments**, `ret 0x34` -- the longest list in the
  widget hierarchy: the buffer, the maximum, four of rectangle, a font, three
  colours, the RETURN handler and two more that every call site passes as
  zero. Its seventh argument is the FONT, as the key row's is, and not a flag.

  **The two character sets are a default and an override, not two tables.**
  The constructor installs the permissive one from `0x00485304` -- with
  `` ` ~ ! @ # $ % ^ & `` in it -- and ENTER BATTLE NAME then overwrites
  `EDIT_OFF_CHARSET` with the letters-and-digits set at `0x00485308`. Reading
  the constructor is what turned two unexplained pointers into one mechanism.

- **The MULTI-SPRITE constructor is reconstructed, and the contradiction it
  seemed to raise was mine.** `0x00456BC0` writes the first bitmap's sprite to
  0x0060 and the second to 0x0064, which looked like it put the array one slot
  earlier than `widget.h`'s note says.

  **The PAINTER settles it, and the painter is A/B-verified**: it reads
  `MULTISPR_OFF_SPRITES + index * 4`, base 0x0064. The note was right. The
  constructor simply has one slot in FRONT of the array, holding the first
  bitmap, which the painter never reads.

  Which makes the widget's behaviour legible for the first time: the SECOND
  bitmap goes into sprites[0] and sprites[1] is left null, so an index of 0
  shows a dot and an index of 1 shows nothing. **The null is the off half of
  the blink**, not an unfilled slot. Reading the consumer beats reasoning from
  the producer, and stopping a turn early to say so cost one turn and no
  wrong code.

- **The SCREEN BASE and the BUTTON are reconstructed** -- `0x00454B00`
  (106 B) and `0x004540F0` (203 B), the two most-executed constructors in the
  menu layer. Every screen starts at the first and nearly every one uses the
  second, so every configuration in the suite runs both.

  The screen base is just a PANEL over the whole display with the dialog
  vtable stamped on top, and its rectangle is `(0, 0, ADDR_SCREEN_W,
  ADDR_SCREEN_H)` -- 640 by 480 READ FROM THE IMAGE rather than written down,
  which is why a backdrop covers exactly the display and not a constant
  somebody chose.

  The button is where the reading needed care. **Only the FIRST of its three
  bitmaps is tested for null**, and a null there also sets 0x0048, which
  `WidgetRepaint` reads as "defer to an ancestor" -- so a button with no
  sprite of its own is drawn by whatever contains it. The other two go through
  unconditionally, so a null there would be stored as a null sprite rather
  than caught. And the normal sprite is copied to the base's own 0x0038 as
  well, the same doubling the panel does.

  With these two closed, the whole chain from a menu request down to a
  rectangle is ours: factory, screen constructor, panel, button, and the
  key row.

- **The key-capture ROW is reconstructed** (`0x00450C50`, 106 B), and **it
  passes an UNINITIALISED byte to the label constructor.** `mov al, byte ptr
  [esi + 0x64]` at `0x00450C5D` reads the object's own 0x0064 before anything
  has written it -- the memory is straight out of `operator new` -- and hands
  it over as the label's ink.

  It is harmless, and knowing WHY took looking rather than assuming, which is
  why this one waited a turn. The label's ink is at 0x0060, not 0x0064, so the
  garbage lands there; the focus label overrides it with its own pair at
  0x0064 and 0x0065, which this function then writes from its arguments.
  Nothing on this class reads 0x0060. Reading it back is faithful and, through
  a `uint8_t *`, is not the undefined behaviour it would be through a wider
  type -- which is the distinction that made it worth checking first rather
  than either reproducing blind or quietly "fixing".

  Two more readings fell out of the argument map: the seventh argument is the
  FONT, and the tenth is used twice -- as the label's paper AND as the colour
  at 0x0066.

- **The PANEL constructor is reconstructed** (`0x00454980`, 136 B) -- the
  container eight of the reconstructed screens hang everything off, and so the
  most-used constructor in the family and the cheapest to check: every
  configuration that opens a dialog draws one.

  **It keeps the sprite twice**, at 0x0038 and 0x0058. 0x0038 is the base
  class's own field -- what `WidgetPaint` draws and what `WidgetRepaint` walks
  the parent chain looking for -- and 0x0058 is the panel's. Reproduced rather
  than collapsed: something reads one of them and nothing here establishes
  which.

  It also named `0x00445CF0`, which is not `PreloadSprite` but a wrapper: it
  splits a bitmap NAME into set, index and frame through `0x0042E310` and then
  calls `PreloadSprite` with the three numbers. That is how a screen can name
  `03_017_00_check.bmp` where the sprite layer wants integers.

- **SELECT PLAYER is reconstructed** (`0x00451400`, 1,247 B) -- the one screen
  whose rows come off the FILESYSTEM rather than from a table or the comm
  object. It chdirs to `save` and walks it with the CRT's `_findfirst` /
  `_findnext`, taking every entry that is a DIRECTORY and whose name does not
  begin with a dot, which is how "." and ".." are skipped without comparing
  whole names.

  Then, once the list exists, **the first row's name is copied into the
  current-player string** at `ADDR_GAMEPROC_BLOCK` -- so opening this screen
  selects a player whether or not anyone clicks. That the campaign still
  reaches MAP 01 is what says the copy works; the widget tree alone would not.

  Reconstructed the turn after the dump that made it comparable, which is the
  order that costs least.

- **`ab.sh campaign` compares the SELECT PLAYER tree now**, ten nodes, and
  until this it compared that screen not at all -- it drove straight through
  on its way to the map and compared only the log and a live-play frame. The
  three defects this session that only `ctl widgets` could name were all on
  screens that HAD a tree dump; this closes the largest screen that did not.

  No screenshot goes with it, deliberately: `campaign`'s pixel budget is -1
  because it ends in live play, and a frame compared against a check that
  cannot fail is not a check.

- **COMM. CHANNEL SELECT is reconstructed** (`0x0042E9C0`, 760 B). Its rows
  come from our own `CommEnumConnections` into a record built by our own
  `RecordCtor` -- with a flag of 1 where DIFFICULTY passes 0.

  **Its list box takes `ADDR_LOG` as a callback, and that is not a mistake in
  either direction.** `orig.h` already records that the linker folded an empty
  virtual and the stubbed varargs logger onto one address, because both are a
  single `ret` byte. Passing it here means "no callback" -- and it is passed
  as the literal address rather than as a null, which is what the original
  does and what we reproduce. A reconstruction that "cleaned it up" to 0 would
  be a behavioural change nobody could see until something read the slot.

  The bar is the arrow-ended one, `ret 0x24`, and the list and bar point at
  each other afterwards -- the list at 0x007C and the bar at 0x0058.

- **ENTER BATTLE NAME is reconstructed** (`0x0042FB00`, 1,082 B), and it
  answers a question the multiplayer code raised: **the two fields edit the
  DIALOG's own buffers in place.** The constructor copies
  `ADDR_SAVED_BATTLE_NAME` into its 0x0064 and `ADDR_SAVED_PLAYER_NAME` into
  its 0x0084 before any widget exists, and hands the edit boxes those
  addresses. That is why `HostBattle` can read the names back out of globals
  afterwards without the dialog passing them anywhere.

  `ret 0x34` on the edit constructor is 52 bytes: buffer, a maximum of 0x18
  characters, sixteen of rectangle, a flag, three colours, a handler and two
  zeroes. **The handler is `ADDR_HOST_BATTLE`, the same function the OK button
  gets** -- so RETURN in either field starts the battle.

  **Two defects, both named by `ctl widgets` and neither visible in a pixel
  count.** The dialog's `focusedChild` is the PANEL, set at `0x0042FC04` right
  after the panel is added -- I had left it unset, which the tree reported as
  `foc=2` against `foc=-1` in its first line. And the focus SLOT is called on
  the first field only, at `0x0042FC9C`, with no such call in the second
  block; calling it for both left the wrong field marked dirty. The frame was
  180 pixels either way, inside the budget.

  That is now three defects in this session that the tree named and the pixels
  could not, against one the pixels named and the tree could not.

  Two smaller findings. `0x00485308` points at
  `" abcdefghijklmnopqrstuvwxyzABC...0123456789!..."`, which every edit box
  takes as `EDIT_OFF_CHARSET`: the field is a WHITELIST, not a length limit.
  And this dialog's buttons are 0x4E wide where every other screen's are 0x51,
  which is why it does not share `MakeButton`.

- **The AUDIO dialog is reconstructed.** `0x0044F370`, 1,208 B, `ret 8` -- two stack arguments, the backdrop
  and the flag, which is what its factory's two-argument call already implied.

  It branches on `ADDR_GAME_STATE` like its factory does, and the branch is
  structural rather than cosmetic: **in a mission there is no panel at all**.
  The dialog itself becomes the parent and the three bars carry an offset of
  (0x89, 0x79); with a panel the panel sits at that position and the bars are
  placed relative to it, so the offset is zeroed. Two stack slots hold that
  offset and are reused for other things afterwards, which is what makes the
  listing hard to follow.

  Each bar is `new(0x80)` then `ScrollBarCtor(bar, rect, parent, 0x92)` --
  `ret 0x18`, so rectangle by value, parent, and a maximum. The rows are at
  +0x38, +0x7D and +0xC2 from the offset, all at x +0x25, 0xBA by 0x15. The
  three are stored on the dialog at 0x0064, 0x0068 and 0x006C, their
  on-change handlers are `0x0044F2A0`, `0x0044F2E0` and `0x0044F320`, and the
  dialog saves the current volumes at 0x0070 and up so CANCEL can put them
  back.

  The position arithmetic is the part to be careful with. `bar[0x74]` comes
  from the volume by a MAGIC-NUMBER DIVISION -- `imul 0x51EB851F` then
  `sar 5`, which is `(volume + 2000) / 100` -- and is clamped at zero. The
  thumb at `bar[0x6C]` is then x87: `fild bar[0x74]`, `fidiv bar[0x78]`,
  `fmulp` against `bar[0x70] - bar[0x64][0x1C]`, and `_ftol` (`0x00464490`).
  `long double` reproduces the 80-bit intermediate, as it does for
  `SetMaxHealth` and `Ticks`.

  The three volume globals were already named: `ADDR_VOLUME_AT_ZERO`
  (effects), `ADDR_STREAM_VOLUME` (music), `ADDR_VOLUME_VOICE`.

  It is the best-checked of the screen constructors, because `audiovol` does
  not only open the dialog -- it nudges the SOUND EFFECTS thumb four times.
  The thumb position IS the x87 arithmetic, and STATUS already records that
  dropping the thumb offset from `ScrollBarPaint` moves 336 pixels on that
  frame. So the division-then-multiply order is compared and not merely
  reasoned about. 13 nodes identical, all three frames at cursor noise.

- **MULTIPLAYER OPTIONS is reconstructed** (`0x00432320`, 968 B): 43
  checkboxes built from the declarative table, then three buttons -- or one.

  Three things depend on being the host, and they are exactly what the two
  panels differ by. A non-host gets `unknown4C` on every box, so none can be
  focused; a non-host gets CANCEL alone, at the OK position; and the pass that
  disables a group whose header is unticked runs for the host only.

  The original walks the table with a cursor four bytes IN, so every field
  offset in the disassembly reads four low and the loop bound is `0x00486BC8`
  where the table ends at `0x00486BC4`. Written here from the record base,
  which is why the numbers do not match the listing on sight -- the same
  cursor offset that made the 43rd record look like a 42-record table when
  `OptionsApply` went in.

- **The CONTROLS dialog is reconstructed** (`0x00450E10`, 689 B) -- the only
  screen in the game whose children come out of TABLES rather than being
  written out one at a time. Three walk together: `ADDR_KEY_BINDINGS` for the
  scancode each row is bound to (a `uint8_t[][2]` walked one byte at a time
  with a stride of two, which is why the loop bound is on the pointer and not
  a counter), `ADDR_KEYROW_POSITIONS` for the row's x and y as int16s, and the
  caption out of `ADDR_KEY_NAME_TABLE`'s second field, selected by our own
  `KeyNameIndexOf`.

  **`ret 0x2C` on the row constructor is 44 bytes** -- index, caption, sixteen
  of rectangle, a flag and four colours. Three of the colours are pushed as
  whole dwords from BYTE loads, so their top three bytes are stale stack, the
  same matched-argument shape `MakeBitmap` has. Safe for the same reason and
  checked rather than assumed: `0x00450C8E` reads all three back as
  `mov al, byte ptr`, so a zero-extended byte is faithful.

  **The defect it shipped with is one the widget tree could not see.** The
  three buttons' flag is the `push 1` in the middle of the block, not the
  `push 0` at the top -- that one is the TRAILING argument. Reading it as the
  flag left the buttons one palette step off: 547 pixels on the dialog frame,
  with the tree **identical at all 25 nodes**. The exact reverse of the
  OPTIONS menu's focus bug an hour earlier, where the tree named it and the
  pixels only said "something". The suite needs both and this pair of defects
  is why.

- **The DIFFICULTY dialog is reconstructed** (`0x0044E730`, 786 B): the
  confirm-dialog shape with a LIST BOX where they have a message.

  It builds its rows with `RecordCtor` and `ListAdd`, both ours -- so the
  record whose missing return took the multiplayer path down for four days is
  now constructed by our code on a screen the suite drives every run.

  Two of the list's fields are seeded from `ADDR_DIFFICULTY` and which two is
  the interesting part: `LIST_OFF_SELECTED` **and** `LIST_OFF_HOT`. The dialog
  opens with the current setting both selected and highlighted rather than
  merely selected, which is the green bar on Medium in a default install. The
  list is also kept on the DIALOG at 0x0064 -- the constructor reaches it
  again twice -- and the blinking dot is stored on the LIST at 0x0094, not on
  the panel that owns it.

- **The three CONFIRM dialogs are one body three times over** -- CONFIRM GAME
  EXIT (`0x0044EB50`), the replay prompt (`0x0044EED0`) and DELETE PLAYER
  (`0x00450730`), 685 bytes each. They differ in five things and nothing else:
  the vtable, the panel's bitmap, the OK handler, the message, and -- for
  DELETE PLAYER alone -- the CANCEL handler. Written once with those five as
  arguments.

  The shape is the thing to know before reading any of them: **the dialog gets
  ONE child, a panel, and everything visible is a child of the PANEL** -- both
  buttons, the typewriter message and the blinking red dot beside it. The
  panel is also what carries the focus, not the dialog.

  Three more widget constructors came with them, and every `ret N` was checked
  rather than inferred from the pushes: the panel's `0x00454980` is `ret 0x18`
  (bitmap, flag, sixteen bytes of rectangle), the typewriter's `0x004566F0` is
  `ret 0x14` (rectangle then message), the two-sprite dot's `0x00456BC0` is
  `ret 0x1C` (two bitmaps, a flag, a rectangle). The rectangle is by value in
  all three, as it is for the button.

  **Three stores are unguarded in the original and are reproduced that way.**
  `panel->flag44`, `panel->focusedChild` and the message's blinker field are
  all written after the allocation was tested and found null on the failure
  path, so a genuine out-of-memory faults there. VC6's `operator new` answers
  null rather than throwing and this game checks it everywhere else, which is
  what makes these an oversight rather than a convention -- but reproducing
  them costs nothing and diverging would be a silent behavioural change.

- **The alias ratchet caught its author again.** All three constructors were
  already named in `orig.h`, from the batch that reconstructed their
  factories, and I gave them second names. I *did* grep the addresses first --
  and grepped the four new callees and the vtables and the string, and left
  the three constructor addresses out of the list. Checking a rule and
  checking every case of it are not the same thing.

- **The first SCREEN CONSTRUCTOR is reconstructed**, `OptionsMenuConstruct`
  (`0x0044FAB0`): a backdrop and four buttons, AUDIO / CONTROLS / DIFFICULTY /
  BACK. The factories were the easy half of this layer; the constructors are
  where the widgets actually come from, and they run 580 to 1,400 bytes each.

  **The rectangle goes to the button constructor BY VALUE, in the middle of
  the argument list**, and the original builds it in place: it pushes the four
  numbers as placeholders, hands RectSet a pointer to them, and overwrites the
  same four slots with what RectSet returns. What settles the reading is not
  the shape of the pushes but `ret 0x28` -- 40 bytes, being three bitmaps, a
  flag, sixteen bytes of rectangle, a handler and a trailing zero.

  And the four numbers are (left, top, WIDTH, HEIGHT), not the four edges the
  type says. `ctl widgets` puts the buttons at 231,160,383,185 and three rows
  below, which is 0xE7 and 0xE7 + 0x98, 0xA0 and 0xA0 + 0x19 -- so the button
  constructor is what turns them into edges. Measured: RectSet stores what it
  is given and cannot tell the difference.

  Written as a table where the original unrolls four copies, because they
  differ in exactly three things and four copies of eleven lines would hide
  that -- **and that is exactly how the one real defect got in.** The FIRST
  button is stored as the dialog's `focusedChild` and only the first, one
  instruction sitting inside block one; a loop over what the four blocks have
  in common drops precisely the line they do not share.

  What NAMED it is `ctl widgets`: `foc=2` on the original against `foc=-1` on
  ours, in the first line of the tree. The pixels said something was wrong --
  294 on the menu itself, AUDIO coming up plain instead of highlighted, over
  the budget of 200 but not by much -- and 305,895 on the dialog frame,
  because with no focused child the click had nowhere to land and CONTROLS
  never opened at all. Neither number says WHICH field; the tree does.

  Compressing repetition is right. Check what the repetition is hiding first.

- **The arm index IS the menu request, confirmed three times by the buttons
  themselves.** The OPTIONS menu's handlers raise 15, 16 and 19 for CONTROLS,
  DIFFICULTY and AUDIO -- which are those screens' arm numbers in
  `docs/screens.md`. The table confirms its own indexing, and the poke that
  reached the host panel with request 7 was not a lucky guess.

- **Two ratchets fired on the commit that landed the last four, and both were
  right.** `checkseams.py`: `RefreshScreen` is already reconstructed, so
  reaching it through an `orig_` macro was a lie about where control goes --
  it would have made a closed seam read as open. `checkglobals.py`: a `const`
  on `g_gameState` that `gameproc.cpp` and `winproc.cpp` do not have.

  Both fixed in the commit after. Worth saying plainly: `make check` was run
  before the change and after the build, but not between the last edit and
  `git commit`, and that is the gap. The ratchets caught it within the minute;
  the discipline they exist to enforce did not.

- **Twenty of the twenty-one screen factories are reconstructed.** The last
  four were the two-argument ones: COMM CHANNEL SELECT (`0x0042EE40`), AUDIO
  CONTROLS (`0x0044F9E0`), DELETE GAME (`0x00450250`) and LOAD GAME
  (`0x00452680`).

  **Where the repaint goes is not the same in all of them, and it was worth
  not tidying.** AUDIO and DELETE GAME call `RefreshScreen` BEFORE they
  allocate; LOAD GAME calls it AFTER constructing and publishes the screen
  only then. Reproduced as written. Whether the ordering matters is not
  something reading settles, and all three screens are reachable, so it can be
  measured rather than argued about.

  COMM CHANNEL SELECT is the one factory that does work before allocating
  rather than around the branch: `CommCreateDirectPlay(comm, 0)`. That literal
  zero is the one `orig.h` already records as making `CommOnConnected`
  unreachable in this build, and it now comes from our code.

  **The twenty-first is not a factory.** `0x0044D730` is the TITLE SCREEN and
  it is 1,108 bytes: it chdirs to `shared`, tears the comm object down, clears
  three globals, and then builds every button on the screen. It belongs with
  the dialog constructors, not with this family. **Done**, and with it every
  arm of `docs/screens.md` reads `yes`: the whole menu screen table is ours.

- **The thirteen plain factories went in as one batch**, generated from the same
  measurements `tools/screens.py` reports rather than transcribed: SELECT MAP,
  SELECT PLAYER, ENTER NAME, the CD prompt, ENTER BATTLE NAME, CHOOSE A
  BATTLE, MOVIES, the OPTIONS menu, CONTROLS, DIFFICULTY, CONFIRM GAME EXIT,
  the replay prompt and DELETE PLAYER.

  Three of them named themselves through their captions rather than a bitmap
  -- "Are you sure you want to quit?", "Do you wish to reattempt your failed
  mission?", "Caution: All saved games for this player will also be deleted!"
  -- and one through both: `0x0042F440` pushes "Copy Protection" and "The
  ARMYMEN2 CD must be in the drive to play Army Men II.", so the menu table
  has a CD prompt in it that is a screen rather than one of the five patched
  `MessageBoxA` sites.

- **`docs/screens.md` is the table, generated.** `tools/screens.py` reads all
  21 arms out of the jump table and reports each factory's allocation size,
  constructor and screen. Nineteen name themselves; the other two are
  identified by their BUTTONS rather than given a name borrowed from whichever
  function sits next in the image.

  Three things it had to learn, each a mistake this repository has recorded
  before. **Find the constructor from the STORE**, not from the first
  `operator new` -- two arms allocate something else first, and one of those
  constructs a helper that has nothing to do with the screen. **Decode
  forward**: walking back byte-wise from the call for a `68`/`6A` finds other
  instructions' operands and reported sizes like `0x8800511A`, which is the
  aligned-dword cross-reference mistake in another costume. And **a fixed
  window cannot bound a function** -- 0xC0 bytes was too short for two arms
  and reading past the `ret` named four screens after their neighbours, giving
  DELETE GAME twice.

- **`tools/ab.sh` has a fourth comparison slot.** It compared the final frame
  plus `mid` and `dlg`; `alt` is the fourth, and `mpoptions` now uses all of
  them -- the host panel, the options dialog before and after DEFAULT, and the
  join panel are four different screens on one run. Without it `OpenMpJoin`
  would have been run rather than compared.

- **`tools/ab.sh` took only its first argument.** `cfgs="${1:-bootcamp}"`, so
  `ab.sh bootcamp controls` ran bootcamp alone and printed "A/B clean" -- which
  reads as both configurations passing. It is `"${*:-bootcamp}"` now. Same
  family as the two missing files that once diffed as identical: a check that
  can report success on work it did not do.

- **The menu is a 21-entry table of screen factories, and none of them is
  named.** `RunFrame`'s menu-request arm dispatches through a jump table at
  `0x00426518`; every arm is seven bytes -- `call <factory>; jmp end` -- and
  every factory is the same shape: destroy whatever dialog is the repaint
  object, allocate, construct on it, store the CONSTRUCTOR'S RETURN into
  `0x0065A058`. That last is the RecordCtor lesson again, twenty-one times.

  Seven name themselves from the bitmap the FACTORY passes -- LOAD GAME
  (`0x00452680`), MP HOST (`0x004317C0`), MP HOST OPTIONS (`0x00432910`),
  MP JOIN (`0x00433480`), CONTROLS (`0x00451210`), AUDIO (`0x0044F9E0`) and
  DELETE GAME (`0x00450250`). The other fourteen open on the shared
  `01_000_00_screen.bmp`, and reading the CONSTRUCTOR's bitmaps instead names
  nine more: SELECT MAP `0x0044DF20`, SELECT PLAYER `0x00451910`, ENTER NAME
  `0x00451E10`, CHANNEL HOST `0x0042F440`, ENTER BATTLE NAME `0x0042FF60`,
  JOIN `0x0042F880`, MOVIES `0x0044E6A0`, the OPTIONS menu itself `0x0044FDD0`
  and DIFFICULTY `0x0044EAD0`.

  Three more -- `0x0044EE50`, `0x0044F220`, `0x00450B70` -- carry only OK and
  CANCEL, so they need their captions read rather than their bitmaps. Two,
  `0x0044D730` and `0x0042EE40`, push no bitmap at all and are probably not
  dialogs; check what they are before assuming the table is homogeneous.

  A good next batch: it is one shape repeated, it gives twenty-one screens
  real names from the program's own vocabulary, and `mpoptions`, `controls`,
  `audiovol` and `difficulty` between them already drive four of the arms.

- **The poke/key asymmetry stands and is not an oversight.** `poke` writes a
  global the game ACCUMULATES, so the write survives and becomes the next
  starting point. There is still no way to set the key buffer, because
  `PollKeyboard` replaces it wholesale every poll. Same reasoning as `cursor`.

- **`COMM_OFF_READY` was `COMM_OFF_IS_HOST` under an invented name.** Both
  named `0x3D8`; the first came from a call site, the second from `DPCAPS_ISHOST`
  and has provenance. `startgame.cpp` sets it to 1 when hosting and 0 when not,
  which settles it. Gone, along with a `const` drift on `g_gameOverFlags`.

- **Four reconstructions had never been installed.** `dist_install` opened
  with `return patch_replace(...)` and had three calls under it;
  `savetag_install` had one. Every tool that reads the sources counted them
  done; the game's own log printed one `patch:` line where there should have
  been four. So `ApproxDistXY`, `AngleDelta`, `RoundTo8` and `WriteSaveTag`
  had never run once, and every A/B that "covered" them compared the original
  against itself. Fixed, and `tools/checkpatches.py` now fails on the shape.
  The three arithmetic ones went live immediately: `ApproxDistXY` 58,
  `AngleDelta` 1,252, `RoundTo8` 4,876 in one Boot Camp mission, `bootcamp`
  clean. `WriteSaveTag` still reads 0 because nothing saves in that run.

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

Beside it, and worked from its leaves rather than from the top, is the
**multiplayer host panel** -- `0x00430530`, 4,497 bytes, declined twice as a
starting point. Its four row-button handlers, the map-selection refresh, the
three data checksums a session compares and both halves of an announcement
are ours; the
constructor itself is not, and is reached by address. Everything here is
compared through `ab.sh mpoptions`, which is the only configuration that
opens the panel at all.

**Sprite loading is finished except one function, and it was the front for a
whole session.** From a set NAME to a drawable record is now ours end to end:
`SpriteSetResolve` picks the archive, `SpriteSetLoad` opens it and builds both
remap tables, `SpriteSetFree` closes it, `SpriteSetForKey` and
`SpriteDirIndex` find a sprite in it, `SpriteLoadFromDataFile` reads the
record, `SpriteLoadTriple` chooses between that and loose files,
`LoadBitmapDescriptor` and `SpriteReloadNamed` do the loose half, and
`SpriteRebuildDf` and `SpriteRebuildAlt` put a lost surface back. What is left
is `0x00423300`, the `.sha` shadow loader, and it is deliberately last: NOT
ONE `.sha` file ships, so its body cannot execute here at all, and at 720
bytes with a 32 KB scratch frame it is the most that could be written with no
way to check the answer.

**The object and item family is the front now, and the groundwork that was
blocking it is partly done.** What unblocked it was not a bigger function but
one FIELD: `obj+0x10` is the army, evidenced by SIX independent callers of
`CommMustBroadcast` that all reach it as `movsx ax, byte ptr [obj+0x10]`. With
that and `OBJ_FLAG_OVERDUE` -- set when an object passes the deadline at
`OBJ_OFF_DEADLINE_58` -- four functions fell out in a row: `DestroyByType`
(0x00428DA0, 22 callers, runs 3 times a mission), `FreeOverdueItems`
(0x00428C40, 69 times a mission), and the two senders beneath them,
`SendObjDestroyed` and `ItemGoneMessageSend`.

**`0x00429320` is written too**, the shared tail of every per-type destroy and
the thing that actually sets the gone flag. It walks a chain of attached
objects with a RECURSIVE self-call, and the flag going on BEFORE the walk is
what terminates it: every chained object re-enters and one already marked
returns at once. The chain is by UID rather than by pointer, which matters --
the object table memmoves its tail on an insert, so a pointer held across the
recursion would be wrong and a uid is not.

Two notes on how it was read, both corrections of mine. I first said
`tools/disasm.py` desynchronises in its middle; it does not -- I had started
the dump at `0x004293A0`, inside the function, and a linear decoder cannot
land on an instruction boundary by luck. From `0x00429320` it decodes 90
instructions to a clean `ret`. And I expected to need a dozen invented names;
in the event only ONE callee was unnamed, because `FindSlot`, `g_objTable`,
`ADDR_ROW_UNREGISTER` and `ADDR_ITEM_PRE_DESTROY_ALIAS` were all already
there. **Check what is already named before estimating what a function will
cost.**

**The destroy dispatch is COMPLETE** -- `DestroyByType`, its shared tail
`DestroyObjCommon`, and all three per-type arms are ours, along with the two
senders beneath them and the overdue sweep beside them.

**And reading each arm's callee before naming it identified two object types
the project had listed as unknown.** `0x0045A770` and `0x0043CA00` are the same
function with one table swapped -- they take an object's footprint back out of
the map's cell weights, gated on a flag they then clear, subtracting 15 per
cell with a stamp so a cell touched twice is only decremented once. One indexes
`ADDR_VEHICLE_MASK` with a kind multiplier, the other `ADDR_ROACH_MASK` with
none, because a roach has one kind where a vehicle has six. The type-3 handler
calls the first and the type-8 handler the second, so **type 3 is a vehicle and
type 8 is a roach** -- evidence rather than proof, since each clearer has other
callers, and both comments say so. Type 2 is still unread.

`0x00458070` is now READ -- 191 instructions, five returns -- and named from
the whole body: `ADDR_OBJ_ATTACH_TO`. It detaches an object from whatever it
was attached to and, given a target, attaches it to that instead, keeping a
membership list on the holder and a stance code on the subject. With a NULL
target, which is how all three handlers call it, it is purely a detach.

Reading it first paid twice. It supplied the name those handlers need, and it
caught `OBJ_OFF_CHAIN_UID` being over-general -- the same two dwords are a uid
pair for items and a count-and-array for types 2, 3 and 8.

It is still not WRITTEN, and that is the remaining judgement call: doing so
needs names for nine fields, three comm methods and four 0x100-byte blocks.
The three handlers need none of that and can go first.

**The one to pick up first, and it is a READ rather than a write.**
`0x00429650` raises `OBJ_FLAG_ON_MAP` and lowers `OBJ_FLAG_OFF_MAP`, the clean
inverse of the already-reconstructed `TakeOffMap` -- and then sets bit 1 on
every row, which is what makes `ADDR_ROW_UPDATE` REMOVE rather than re-link.
An object going back onto the map unregistering its own rows does not read
right. All three candidates are READ now and none dissolves it -- 
`ADDR_ROW_UNREGISTER_ALL` is correctly named, `ObjFlagBit0` is `row->flags & 1`,
and `0x0041DD90` is the dirty-rectangle collector and touches no flag. So the
branch stands, and the pair are exact opposites whose object-level names
contradict them. **Done, and the whole family was inverted.** `0x0041A1B0` walks the registry
and applies one of the two functions to every enemy object, chosen by a flag it
flips on entry -- and its only two callers are arms of the cheat table, which
name the pair outright:

    "I see everything!"                 -> reveal every object
    "I bury my head 'neath the sand."   -> conceal every object

So this was never map registration at all. It is the **fog of war**, `0x0200`
is CONCEALED, `0x0800` is REVEALED, and the evidence is the game's own strings
rather than another reading of the same two bodies. Renamed together:
`OBJ_FLAG_ON_MAP`/`OBJ_FLAG_OFF_MAP` -> `OBJ_FLAG_CONCEALED`/`OBJ_FLAG_REVEALED`,
`TakeOffMap` -> `RevealObj`, `TakeNearbyOffMap` -> `RevealNearby`,
`OBJ_OFF_RETURN_AT` -> `OBJ_OFF_REVEALED_UNTIL`, and `ADDR_AI_CONTROLLED` --
itself a name off a call site -- -> `ADDR_FOG_OF_WAR`. `ADDR_OBJ_CONCEAL` and
`ADDR_TOGGLE_FOG_OF_WAR` are named for the first time.

Everything that looked asymmetric dissolves: `RevealNearby` skips an object
already carrying `0x0800` because it is already revealed, and the sweep reads
the stamp it leaves to decline concealing one whose window is still open. An
air strike lights up what it flies over.

One family error came out with it. `air.cpp` cleared a ROW's bit 1 using
`OBJ_FLAG_OVERDUE`, which is an OBJECT flag in a different struct that merely
shares the value 0x02. It is `ROW_FLAG_REMOVED` now.

**`ObjAnchorPoint` (`0x00403AF0`, 80 bytes) is reconstructed**, found while
reading toward `0x00404730`. It answers an object's position with its sprite's
second anchor pair subtracted, and it picks row ONE when the object has more
than one row -- reproduced rather than tidied. Two new offsets came with it,
`ROW_OFF_SPRITE` and `ROW_OFF_PREV_SPRITE`, identified from `ADDR_ROW_UPDATE`
comparing the two alongside the current and previous rectangles.

**It is installed and it does not execute.** The counter exists and reads 0
after 57,508 composed frames of live Boot Camp, and so do `RevealObj` and
`RevealNearby`: all three callers are original code, so this is not the
caller-is-ours blind spot -- the whole air-support and fog cluster is simply
dormant on every drive this project has. Verified by reading and by the A/B
showing no change, which is weaker than the rest of the tree and is said
plainly rather than left to be inferred from a zero.

**A two-name alias was resolved on the way.** `0x005125A0` carried
`ADDR_DEFAULT_SOUND_POS` and `ADDR_PAD_DEFAULT_POS`, both from call sites. It
is `.bss`, 103 sites read it and **nothing in the image writes it**, so it is
permanently `{0,0}`: it is `ADDR_ZERO_POINT`, and every reader is asking for
"no position". `MAX_ALIASES` is 21.

**The cheat table is now in `orig.h`**, because it is the strongest naming
evidence this image has. 40 words dispatching through a 39-entry jump table,
with `when all else fails...` as the master switch -- and words 3 and 4,
`spidey senses tingling` and `moleman`, are what settled the fog of war.

**`ResolveFormationPoint` (`0x00404580`) is reconstructed, and the cluster has
a name now: FORMATION.** What identified it was the table `0x00404400` indexes
-- twelve 6-byte entries at `0x00473EA0`, matching that function's own
`slot < 12` guard, and every one decodes to a squad position:

    0 behind 64    3 behind 96    6 right 64    9 behind-left 96
    1 b-left 48    4 f-right 96   7 left 64    10 behind-right 96
    2 b-right 48   5 f-left 96    8 FRONT 128  11 behind 128

Every facing a multiple of 45 degrees, every distance 48, 64, 96 or 128, added
to the leader's own facing so the formation turns with it, and doubled for a
type 3 -- vehicles get twice the spacing. So `OBJ_OFF_FORMATION_SLOT` (`0xA0`)
and `OBJ_OFF_FOLLOW_UID` (`0xC4`) are named, and `0x00404730`'s five rejection
tests are revealed as "is my leader still worth following".

The reconstruction itself is the small half: a leader who is RIDING something
is not the thing to follow, so a type 2 with a non-zero `OBJ_OFF_RIDING` is
looked up and the vehicle takes its place. `ADDR_FORMATION_POINT` below it
stays original and is reached by address.

**It is dormant, and for a reason worth stating rather than a bare zero.** Its
three callers are all original code, so the counter is not blind -- it reads 0
after 76,194 composed frames of Boot Camp. Both drivable missions start with a
squad of ONE: the HUD's SQUAD panel shows only Sarge on Boot Camp and on
campaign MAP 01 alike, and formation code needs a follower. Reaching it needs a
mission far enough in for Sarge to have squadmates, which is a drive this
project does not have -- the same shape as `RemoveFromItemList` needing
something to die.

**`FormationPoint` (`0x00404400`, 384 bytes) is reconstructed**, which completes
the formation pair. The slot's facing is added to the leader's, the distance is
doubled for a type 3, and a type-3 leader gets one extra rule: a follower whose
slot lies within a quarter turn of the leader's heading -- in FRONT of it -- is
swung 0x3D or 0xC3 aside by the sign of the delta and pushed 0x20 further out.
No standing in front of a moving vehicle. The result is clamped to the map
bounds (`ADDR_MAP_BOUNDS_*`, four int32 read as one block out of the map file)
and settled onto a tile.

**My first hand-trace of its stack was WRONG, and a tool is what caught it.**
Reading the frame by hand gave slots that contradicted each other -- the second
`AngleDelta` argument came out as the `out` pointer, which is nonsense. A depth
tracker over the disassembly showed why: my count had followed the early-return
path through `add esp, 0x10` and `pop esi`, which the `slot < 12` branch jumps
over. On the real fall-through the slots are consistent, and the argument is
the leader's own facing. **Do not hand-count a frame across a branch you do not
take** -- and the giveaway was that the wrong reading produced an absurdity
rather than a plausible one, which is luck.

Two details that would each have been a defect. `ADDR_ANGLE_DELTA` masks both
arguments with 0xFF, which is what makes the original's dirty dword there
harmless -- it stores a facing BYTE over a slot still holding pointer bytes.
And the original keeps the whole expression in x87 and truncates once through
`_ftol`; done here in double, which is exact for these magnitudes, with a C
cast for the truncation.

**Neither of the pair executes, and the A/B cannot see them.** 69,467 composed
frames of Boot Camp with both counters at 0. `FormationPoint`'s other caller,
`0x0043E0EF`, is original so its counter is not blind -- it simply is not
reached. Both drivable missions start with a squad of ONE. So this is the
largest piece of unexercised arithmetic in the tree and it is verified by
reading alone; the A/B being clean says nothing about it either way, and is
reported as no-regression rather than as coverage.

**`ClearFrameCounts` (`0x004035F0`) is sixteen bytes that clear two counters,
and BOTH are vestigial.** The whole image -- not just below the CRT line --
holds exactly three references to the pair: the two writes here, and one read
of `ADDR_PERFRAME_COUNT_A` at the top of `0x00403B40`, which gives up when it
is above 10. Nothing increments either. So the first is permanently zero and
that guard is dead code, and the second is written once a frame and read by
nothing at all. 18,069 calls a mission: two stores to nothing, eighteen
thousand times.

**That is the third piece of consumerless bookkeeping on this one path.**
`ADDR_SECOND_DEADLINE` is writes with no reader; `ADDR_FIXED_STEP` is reads
with no writer; this is both at once. Whatever the per-frame block once did, a
good deal of it was cut and the scaffolding left standing -- which is worth
knowing before reading any of it as meaningful.

**`MissionInput` (`0x00424CA0`) is in-mission input -- escape, the F1 info
bitmap, and mouse edge scrolling** -- 16,086 calls a mission. The action is
`0x14` and the Boot Camp dialog names it on screen: "HIT F1 DURING GAME FOR
MORE INFO". Outside a network game it pauses; inside one it does not, since one
player must not stop everyone else's clock. The edge scroll moves
`ADDR_VIEW_TARGET` by the same `speed * frame delta` step `ViewUpdate` glides
the eye with, so the pair agrees by construction.

**It shipped two inverted conditions and the A/B called it CLEAN twice.**
`mission` has no pixel budget -- two unsynchronised scrolling runs differ by a
quarter of the frame -- so the only signal was a line the suite prints and
never acts on: `frames 4771/18641`, where every other run this session was
about 5,000 on both sides. Reverting to the previous commit gave 5,587/5,572
and 290 pixels, which is what turned a suspicion into a measurement.

The defects: dismissal is `flag8 || (button AND changed)` and I wrote the pair
as an OR, so the sign went away on the first mouse MOVE; and the scroll guard
is `following AND button` and I wrote `AND NOT button`, so it edge-scrolled
exactly when it should not. Fixed, and `mission` is back to 4,948/5,633 at 302
pixels.

**`ab.sh` now CHECKS the frame counts instead of only printing them.** More
than a factor of two between the two sides fails the run and says to read the
number before believing the verdict. The honest spread across this session is
1.00 to 1.14; the defect was 3.9. Tested in the failing direction with
`AM2_AB_FRAME_RATIO=99`, which flips the verdict from "A/B clean" to "A/B found
differences".

**`ADDR_ACTION_KEY_RELEASED` nearly went in under a name already in use.** It is
the third of a family -- `0x004274F0` is "down", `0x00427530` is "just
pressed", `0x004275B0` is "just RELEASED" -- and I had typed
`ADDR_ACTION_KEY_DOWN`, which already names the first. **Grepping the address is
not enough; the NAME has to be grepped too**, or two different functions end up
under one name, which is worse than an alias and which no ratchet here catches.

**And my own "is this named?" one-liner was unreliable all session.** It took
the FIRST grep hit, which is often a comment mentioning the address rather than
the `#define`, and my `sed` then stripped the line to nothing and reported
`UNNAMED`. The ratchets caught every consequence -- `checkpatches` held at 21
aliases throughout -- but the method was wrong. Match `^#define.*0xADDR`.

**`ViewUpdate` (`0x0042B5A0`) is THE CAMERA****`ViewUpdate` (`0x0042B5A0`) is THE CAMERA**, 15,534 calls a mission. Clamp
the target so half a screen either side of it is still on the map; move the eye
toward it by at most `speed * frame delta in seconds`; clamp the eye the same
way; then derive three rectangles -- the view in world coordinates, that
shifted by the blit origin and sized to the SCREEN, and the two intersected
with the map bounds.

**The audio listener IS the camera centre.** `ADDR_LISTENER_POS` -- named "the
ear" long ago -- is the point this glides, so sounds are heard from wherever
the view is looking. One address, two true readings, and it keeps the one name
it had; the note is in `orig.h` rather than in a second name.

**And `ADDR_FRAME_DELTA_SEC` reads as an identity here.** Renaming it away from
`ADDR_SHAKE_RATE` three commits ago was worth doing for exactly this: the
camera speed is units per second times the frame delta in seconds, which is
obvious under the new name and was not under the old.

Two one-shot flags that are not the same: `ADDR_VIEW_SNAP` teleports the eye
and clears itself; `ADDR_VIEW_HOLD` skips the distance-limiting arithmetic for
one frame and clears itself, but moves nothing.

**`ab.sh mission` is the run that matters here** -- it is the configuration
that SCROLLS, so the glide, both clamps and every derived rectangle are
compared against the original rather than sitting still. Clean at 281.

**The install failed silently again and the log caught it again.** Same shape as
`SeqRunBoth`: the edit did not match `mapdraw_install`'s opening line, `counts`
said `(nothing traced)`, and the log said 856 patch lines against
`checkpatches`' 857. Checking that pair is now routine, and it has paid twice.

**`AiTakeAbandoned` (`0x0043B7C0`) is the AI taking over armies**`AiTakeAbandoned` (`0x0043B7C0`) is the AI taking over armies whose players
have gone.** Four army records at `AM2_PLAYER_STRIDE`, and two conditions per
army, both needed: `COMM_ARMY_OFF_WAS_HERE` set, so somebody once held it, and
`CommSlotHasPlayer` false, so nobody holds it now. An army that was NEVER
occupied is left alone -- which is what stops the AI being handed every empty
slot at the start of a session.

The callee parses that army's `.aai` through `DefParseInfoFile` and complains
`"Couldn't parse %s!"` if it will not read, which is what identifies the whole
thing: this is CLAUDE.md's "left, AI takes over" from the other end.

**There is no "already done" flag**, so this reloads the `.aai` every frame for
as long as an army stays abandoned -- unless loading it clears `WAS_HERE`,
which is the callee's business and is not established. Recorded so a repeated
parse is not later read as a fault.

Counter reads 0, which is the expected answer and not a surprise:
`TakeMenuRequest` guards the call on `ADDR_NET_GAME`, and this project cannot
start a session. Verified by reading; confirmed INSTALLED the other way, against
the log -- 856 patch lines, 856 from `checkpatches`.

**`MissionStartup` (`0x00444EF0`) raises the level's `startupN` script event**`MissionStartup` (`0x00444EF0`) raises the level's `startupN` script event
and then autosaves.** The event name is BUILT rather than looked up -- the
level index forced to 1 when not positive, so a level that set none still fires
`startup1` -- and a script declaring no such name simply raises nothing.

The autosave has three guards and any one cancels it; the interesting one is
`ADDR_WIN_ENABLED`, so the game stops autosaving once winning is enabled. The
filename is then copied into `ADDR_GAMEPROC_STR_B`, which lives inside the
block the savegame itself writes -- so the name of the last autosave survives
into the next save.

**The autosave is attributed, not assumed.** The counter reads 1 per mission on
either drive, but Boot Camp writes no `.sav` at all, so a guard cancels it
there and which one is not established. On the campaign a file appears -- and
to be sure that was OURS rather than the A/B's original half, I backdated the
existing file to 2020 and ran a single patched build: it came back stamped with
the current minute at the same 176,850 bytes.

**A file mtime moving during a two-sided A/B says only that somebody wrote it.**
Backdating the artefact and running one side is what turns it into evidence.

**`SeqRunBoth` (`0x00461930`) is the per-frame seq step****`SeqRunBoth` (`0x00461930`) is the per-frame seq step** -- 19,066 calls
against `ComposeFrame`'s 19,144. It runs the walker at `0x00461870` over two
contexts; that walker stays original. Its records are 48 bytes with a kind at
`+0x00` dispatched through an eight-arm jump table, a gate at `+0x08`, and a
next index at `+0x2C` -- and every arm RETURNS the next index, so the walk is
index-chained rather than sequential.

**"Seq" is the program's word, not mine.** It comes from `"Couldn't Blt Seq
Pixels"` a few hundred bytes further on in the same band, which is what makes
the name evidence rather than invention. What a seq IS remains unestablished
and the comment says so.

**And it was very nearly a reconstruction that never ran.** My edit adding the
`patch_replace` did not match `misc_install`'s opening line and silently
changed nothing, so the function compiled, linked, and was never reached.
`counts` answered `(nothing traced)` -- which reads exactly like a full trace
table -- and the game's own log settled it: **853 patch lines where
checkpatches counts 854.** CLAUDE.md already records four reconstructions that
had never been installed, found the same way; this is a fifth, by a different
mechanism, and the lesson holds unchanged. **A patch list is a list of
intentions; the log is the list of installs.**

**`HudUpdate` (`0x00414370`) completes the HUD pair****`HudUpdate` (`0x00414370`) completes the HUD pair** -- the same three widgets
in the same order with the same null test on the third, but through vtable slot
2 rather than slot 1. 19,324 calls beside `HudPaint`'s 19,406 and
`ComposeFrame`'s 19,492.

Slot 2 takes no arguments at all, which is why the two functions look so
different in the disassembly despite doing the same walk: the paint pass pushes
a rectangle by value and the update pass is a plain thiscall.

Two further steps follow the widgets and both stay original, each with exactly
one caller -- this one -- so their names cannot be wrong about anything else.
They are still ROLES rather than recovered names, and are labelled as such:
neither pushes a string. A little is established about the second, which is the
tail JUMP: it walks 0x64-byte records at `0x004FC8E0`, clears those whose
deadline at `+0x30` has passed, and stamps the cursor position into the live
ones. What they are FOR is not established, and the comment says so rather than
guessing a name that would read as knowledge.

**`HudPaint` (`0x004143A0`) -- the function declined last commit**`HudPaint` (`0x004143A0`) -- the function declined last commit, now settled
and reconstructed.** 19,177 calls against `ComposeFrame`'s 19,257, and the HUD
it produces is correct by eye: minimap, both panels, the portrait and its
stats, the command bar.

**The stack puzzle was my misreading and the answer is one line above it.** The
first of the three paint calls does `sub esp, 0xc` and then writes SIXTEEN
bytes of rectangle, which looks like a four-byte overrun leaving the epilogue
unbalanced. It is not: `SetDrawTarget`'s pushed argument is **never cleaned
up**. That stale dword is still on the stack, the compiler counts it as the
last quarter of the struct, and the callee pops all sixteen. It balances
exactly, and the other two reserve the full `0x10` because by then there is
nothing stale left to reuse.

Worth having stopped for it. Declining to reconstruct what I could not explain
cost one commit; writing it from the wrong reading would have cost a confident
comment asserting an overrun that is not there.

**`FrameClockStep` (`0x00424B20`) is THE GAME CLOCK****`FrameClockStep` (`0x00424B20`) is THE GAME CLOCK** -- 19,803 calls against
`ComposeFrame`'s 19,977. It measures the frame, clamps it to 66 ms, adds it to
`ADDR_GAME_CLOCK_MS` and publishes the delta in both units. Everything that
treats `0x00511E04` as "now" -- the timers, the pads, the audio -- is being
driven from here, which also makes it self-evidencing: a wrong delta would
stall or race all of them at once.

**`ADDR_SHAKE_RATE` was a name off its one reader, and is renamed.** It holds
the frame delta in SECONDS -- `delta_ms * 0.001`, written here -- and its
millisecond twin `ADDR_FRAME_DELTA_MS` was already named four bytes away. Two
confirmations: the constant is exactly 0.001, and `TakeMenuRequest` forces the
field to `0x3D872B02` = 0.066 in a network game, which is the same 66 ms the
delta itself is clamped to. The screen shake integrates its phase per second
and so wants exactly this value; that is a use, not an identity.

**A dead switch, the mirror of last week's dead deadline.**
`ADDR_FIXED_STEP` would substitute a flat 16 ms for the measurement. Below the
CRT line it is READ three times and written nowhere, so the game is always
wall-clock timed. `ADDR_SECOND_DEADLINE` was writes with no reader; this is
reads with no writer.

**And `Ticks` collapsed from thousands to FOUR** on the same run, because this
calls it by name. The largest instance of the first blind spot in the tree so
far, and nothing about the behaviour changed.

**`TimerTick` (`0x0041E950`) fires the timer table****`TimerTick` (`0x0041E950`) fires the timer table**, at most once every 100 ms
and 23,901 times against `ComposeFrame`'s 24,056. The gate is a SUBTRACTION
against the last sweep rather than a comparison with a deadline, so it survives
the clock wrapping -- the timers' own due times are compared directly and do
not. `removeevent` is `count == 1` evaluated BEFORE the decrement, so the event
registration is torn down by the same call that delivers the final tick.

**And the obvious counter would have misled me.** `CreateTimer` reads 0 on the
same run, which looks like "no timers exist" and is worth nothing: its only
caller in the image is our own `EventTriggerDelayed`, calling by name, so that
counter cannot move at all. A probe settles it instead -- **three timers fire**,
slots 0, 1 and 2, every one with `count == 1`. So the firing path runs, the
`removeevent` argument is exercised in its TRUE form, and the slot-free branch
runs three times. The repeating branch does not: no timer here has a second
tick.

That is the distinction from `FlameTick` below drawn the other way. There the
call count was high and the body dead; here a callee's zero suggested the body
was dead and it is not. **Neither number means anything until you know which
counters can move.**

**`FlameTick` (`0x00417810`) is the "Flame On!" cheat's per-frame effect****`FlameTick` (`0x00417810`) is the "Flame On!" cheat's per-frame effect**, and
that is what identifies every global in it -- the two cheat arms that write
`ADDR_FLAME_ON` and its clock are at `0x00417E20` and `0x00417EF0`. Every 200 ms
it points the army leader's weapon field at `ADDR_FLAME_RECORD` and fires
effect `0x14A` one tile ABOVE the leader, not at its feet. The clock advances
from NOW rather than from the previous deadline, so bursts drift with frame
timing instead of keeping cadence.

**Its null test is in the wrong place, and that is the original's.** The leader
is dereferenced for its position twice and only THEN tested against zero -- so a
run with no leader faults before reaching the guard, and the guard protects
nothing it is placed to protect. Reproduced in order, the same class of latent
fault as `LookupOwnerObj`'s untested result in `DamageObject`.

**19,893 calls, and every one returns at the first line.** It tracks
`ComposeFrame`'s 19,970, so it really is per-frame -- but the cheat is off, so
nothing past the flag is exercised, including that misplaced test. **A high
call count is coverage of the ENTRY, not of the body**, and the two are worth
separating whenever a function opens on a flag. The previous four commits could
all claim body coverage; this one cannot, and says so.

**`PadAdvanceDeadlines` (`0x00437A50`) and `RefreshObjCtx` (`0x00425E70`)****`PadAdvanceDeadlines` (`0x00437A50`) and `RefreshObjCtx` (`0x00425E70`)**,
two more per-frame steps at 18,546 and 18,617 against `ComposeFrame`'s 18,699 --
predicted from the caller again, before either was written.

**A bare address resolved into two struct fields.** The pad loop walks
`0x005161D4` with a stride of `0x48`; that is `ADDR_PADS` plus `0x3C`, and 72 is
`AM2_Pad`'s known stride -- so the two dwords it touches are fields `+0x38` and
`+0x3C`, now named `period` and `dueAt`. A deadline advances by ONE period per
frame rather than to the next future multiple, so a pad that has fallen behind
catches up a step at a time.

**`RefreshObjCtx` carries what looks like a copy-paste slip in the original,
and it is reproduced.** Three context slots are re-resolved from their uids;
the first and third clear their UID when the lookup fails, so the slot stops
being retried. The middle one writes the null back over its own CACHE instead
-- which already holds it -- and leaves the uid alone. The consequence is that
a stale uid there is looked up again every frame forever. Kept exactly, with
`/* sic */` on the line, since fixing it would change how often a dead uid is
searched for.

It also calls `LookupOwnerObj` and DISCARDS the result -- the next call
overwrites the register before anything reads it -- so that call runs for
whatever it does on the way, not for what it returns.

**`ObjFrameSweep` (`0x00428700`) and `AdvanceSecondDeadline` (`0x00424FE0`) are**`ObjFrameSweep` (`0x00428700`) and `AdvanceSecondDeadline` (`0x00424FE0`) are
reconstructed, both once a composed frame** -- 17,716 and 17,791 against
`ComposeFrame`'s 17,866.

**And this time the coverage was predicted before the code was written.**
`Update3DAudioVolumes` is already reconstructed, already known to read five
figures, and sits on the same path -- so an existing counter said the caller
was hot without a probe, a drive, or a guess. That is what the last two
mis-picks were missing, and it costs nothing.

The sweep bumps `ADDR_ITER_STAMP` BEFORE the walk and exactly once, which is
what lets a per-object step tell "this frame" from "some earlier frame" without
carrying a frame number. Its comm check is a tail JUMP in the original, which
for a void function with no arguments is the same as a call and a fall-through.

`AdvanceSecondDeadline` pushes a deadline a second further out once the clock
passes it -- **and nothing reads that deadline.** Below the CRT line
`0x005122F8` has exactly three references: the seed, and the two in this
function. Kept because it is on a path that runs every frame and its absence
would be a difference even though its presence is not.

**`TakeMenuRequest` itself was declined**, and the reason is worth recording:
it is verifiably hot, but fifteen of its callees are unnamed and only one names
itself in a string. Fourteen invented names in one commit is how the damage
family went wrong. Its small callees are the better unit, and two of them are
this commit.

**`MissionPausedFrame` (`0x00425CD0`) is reconstructed and UNEXERCISED**`MissionPausedFrame` (`0x00425CD0`) is reconstructed and UNEXERCISED, and I
mis-picked it the same way as `CommDrainMsgs`.** "Runs every frame while the
game is paused" is true and useless: Boot Camp's opening dialogs have sub-state
arms of their own, so the combination this arm needs -- sub-state 33 AND a
pause -- never arises on any drive here. The counter reads 0 with both dialogs
up and 0 again in play.

**Reading the condition a caller calls under is not the same as observing that
it holds.** That is the third clause of the seam rule, and I had already
written it down once.

The function itself is worth having read. ESCAPE leaves, tested as RELEASED and
clearing EVERY pause bit rather than the one that caused the pause. The
map-wait bitmap is loaded, drawn and FREED inside one call, with the slot
cleared on both sides of the load -- so it is not cached at all, and what stops
it reloading every frame is the pause bits changing rather than the slot being
occupied.

Two names were nearly duplicated and both were caught by grepping first:
`0x00425CD0` already had `ADDR_SUBSTATE33_ALT` (renamed, since the body beats
its position in a table), and `AM2_DIK_ESCAPE` already existed as a local in
`widget.cpp` (promoted to `orig.h`, so both testers share one definition).

**`checkseams` was green over a real seam**`checkseams` was green over a real seam, and the bug was in its own
continuation joiner.** `join_continuations` folded exactly ONE `\`-continued
line: after each fold it appended a blank, so the next line saw `out[-1] == ""`
rather than the line it had just extended. A macro continued TWICE therefore
kept its tail on a line of its own -- with the joined half still ending in a
backslash and containing no `ADDR_` name at all.

CLAUDE.md already records this check being fixed once for exactly this class of
miss ("six real seams the gate had been green over"). This is the same failure
one level deeper: the fix then handled one continuation, and nobody asked about
two.

It was hiding `orig_comm_army_of_slot`, which has been reaching our own
reconstructed `CommArmyOfSlot` through the image for as long as both existed --
two call sites, in `msgslot.cpp` and `widget.cpp`. Both now call it directly.

Fixed by folding until the accumulated line no longer ends in a backslash, and
**tested in the failing direction**: with a three-line seam macro restored the
tool exits 1, without it 0. Only one such seam existed, but a gate that cannot
see a whole spelling is worth more than the one thing it was hiding.

**`DeployItem` (`0x00428CA0`, seven callers) is reconstructed****`DeployItem` (`0x00428CA0`, seven callers) is reconstructed**, and it names
itself in its own resurrection log. Put an object into the world and tell the
other machines; `resurrect` takes a revive path that is unusually suspicious of
its caller -- it logs the uid and health, and if the health is NOT zero it logs
a second complaint and gives up UNLESS the object is flagged destroyed. So an
item that is alive and unmarked is refused, while one whose health outlived its
destruction is healed to full.

**The string sweep paid again**: `0x00449250` is `DeployTrooper` and
`0x0042AA50` is `itemDeployMessageSend`, both from their own text. The vehicle
arm carries no string and takes its name from the dispatch index, the same way
`ADDR_DAMAGE_VEHICLE` did -- a method now confirmed twice by a sibling that
does carry one.

**An absent log line is evidence.** The counter is blind, `EvtDeployItem` being
ours, so it says only that the caller ran once. What settles the rest is that
not one `DeployItem(resurrection)` line appears in a whole run -- direct
evidence the revive path is untaken, rather than an inference from the caller's
arguments. That works because the line is unconditional on the path in
question, which is the condition for reading silence as a measurement.

**`NotifyHealed` (`0x00427E80`) completes the notifier family****`NotifyHealed` (`0x00427E80`) completes the notifier family** -- kinds 4
killed, 5 damaged, 6 healed, all three now ours and all three the same shape:
one event for the object, a second party's triple when there is one, and a zero
delay so none can take `EventNotify`'s delayed path. That they are identical is
what made the family readable; the only thing separating them is the literal.

Its counter is 0 and always will be -- `HealObject` is the only caller and
calls it by name -- so coverage is transitive from `HealObject`'s earlier
probe: one call, non-item path, null `src`, so the two-party arm does not run.

**And it explains a counter that fell to 0 in the same run.** `ObjEventMask`
read 1 last commit and reads 0 now. The cause is this very function: that one
call arrived THROUGH the original `0x00427E80`, which is now ours and calls
`ObjEventMask` by name. A counter dropping to zero alongside a change that
looks unrelated is exactly the shape that gets misread as a regression, so the
chain is written down rather than left to be re-derived.

**`RowAlloc` (`0x0041D2B0`, six callers, 2,512 calls a mission) is**`RowAlloc` (`0x0041D2B0`, six callers, 2,512 calls a mission) is
reconstructed** -- the row's constructor: size the entry buffer, fill it in,
work out the rectangle and register.

The sizing is the interesting part. A width and height in world units become a
cell count by taking 2 off each FIRST -- so a span that exactly fills a cell
boundary does not claim the next one -- then shifting down by 8 and adding 2
back for the partial cells at either end. The multiply is an 8-BIT `imul` with
only AL kept, so a row needing more than 255 cells wraps. Reproduced: nothing
here comes near it, and a wider type would be a silent behaviour change.

**And it disagrees with `RowUpdate` about the rectangle.** This one subtracts
the sprite's hot spot and stops; `RowUpdate` also subtracts `ROW_OFF_Y_ADJUST`.
Both are the original's, and this is the one that runs first -- so whatever
that adjustment is, it only takes effect once `RowUpdate` has seen the row.
Reproduced rather than reconciled, since making them agree would change one of
them.

`RowRegisterAll` went 1,587 to 0 with this commit, this having been its only
caller: the fifth member of the family to fall silent that way. `maprow.cpp`
now has exactly two counters that can move, `RowAlloc` and `RowUpdate`.

**`RowRegisterAll` (`0x0041D980`, 1,587 calls a mission) is reconstructed****`RowRegisterAll` (`0x0041D980`, 1,587 calls a mission) is reconstructed**,
the counterpart of `RowUnregisterAll`. It shares `RowUpdate`'s cell arithmetic
exactly -- including the COLS-for-ROWS clamp -- which is what makes that quirk
convincing as a deliberate copy in the original rather than a slip in one of
them. No unlink and no re-sort, because nothing it handles is placed yet; and
it clears surplus entries harder, both list links as well as the cell.

**With it the subsystem is CLOSED, and every counter in the file now reads 0
except `RowUpdate`.** `DepthLink` went 2,758 to 0 because this was its last
original caller; `DepthCompare`, `DepthResort` and `RowUnregisterAll` went the
same way in earlier commits. That is what a finished subsystem looks like from
the outside -- and it is indistinguishable from a broken one unless the numbers
before it are on record. They are, in each commit that took one to zero.

**`RowUpdate` (`0x0041D480`, THIRTY-SEVEN callers) is reconstructed at**`RowUpdate` (`0x0041D480`, THIRTY-SEVEN callers) is reconstructed at
242,936 calls a mission** -- the hottest thing here by two orders of magnitude,
and the centre of the row/cell subsystem. It brings one row's membership of the
map's cell grid up to date: four early exits, then the sprite rectangle placed
at the row's position, shifted down by 8 into cells, clamped, and then a double
loop that links each covered cell, re-sorting an entry already in the right one
and unlinking any that has moved. Surplus entries are trimmed at the end.

One clamping asymmetry is the original's and is REPRODUCED: the bottom edge is
clamped against COLS-1 where the top was tested against ROWS-1. Every shipped
map is square, so nothing here can tell the two apart -- the same situation
CLAUDE.md records for `ADDR_MAP_TILES_W`, and correcting it would be a
behaviour change defended by a guess.

**`selftest-link` forced a module move, and it was right to.** `air.cpp` and
`item.cpp` call `RowUpdate`, and a flat module may not reach a `win32/` header
-- so a declaration-only header looked like enough. It is not: `make check`
builds `tests/selftest.exe` from the flat sources alone, and that link cannot
see a symbol defined in `win32/`. **The split is a LINK-TIME fact, not only a
naming convention.** So `DepthCompare`, `DepthLink`, `DepthResort`, `RowUpdate`
and their helper now live in a new flat `src/game/maprow.cpp` -- which is where
they always belonged, since not one of them names a platform type. Two commits
ago I wrote that leaving them in `win32/` was a considered choice; the check
disagreed and the check was correct.

**`DepthLink` (`0x0041D8F0`, 2,888 calls a mission) is reconstructed****`DepthLink` (`0x0041D8F0`, 2,888 calls a mission) is reconstructed** -- the
list primitive that puts a node which is NOT yet in the list into its sorted
place. Same four-exit shape as `DepthResort`, minus the unlink, and it only
ever walks forward, because a node that is not linked has no position to walk
back from.

**Grepping the address stopped a wrong assumption, not a duplicate.** A
`DepthInsert` already exists in the tree, so the counter dump made this look
like work already done. It is not: that one is `0x0041E160` and takes an object
and a world rectangle. This is the layer beneath it. The two names are close
because the operation really is split in two -- which is exactly why the
address, not the name, is what settles it.

**And `DepthCompare`'s counter went from 12,661 to ZERO between two commits,
with no behaviour change.** Its only two live callers are `DepthResort` and
`DepthLink`, and both now call it by name instead of through the patched entry.
A textbook first-kind blind spot, caught as it happened -- without the previous
run's number in hand it would read as a function that had stopped running.

**`DepthResort` (`0x0041DB90`) is reconstructed and it is the hottest thing**`DepthResort` (`0x0041DB90`) is reconstructed and it is the hottest thing
here: 3,922 calls in a Boot Camp mission**, with `DepthCompare` at 12,661 on
the same run -- about 3.2 comparisons per call, so the walk loops are doing
real work rather than every node landing on the first test.

It puts one node back into depth order after the object it points at has moved:
an insertion sort's inner loop run alone, walking outward in whichever
direction the first comparison indicates. Four exits, and they are not
symmetric -- only the walk-to-the-front case writes `*head`, because a node
inserted between two others cannot become the head. The unlink differs by
direction too: going forward `n->next` is known non-null, going backward
`n->prev` is, and the original writes each accordingly rather than guarding
both.

It sits in `win32/mapdraw.cpp` despite naming no platform type, because
`DepthCompare` does and a flat module may not reach a `win32/` header even
transitively. Splitting a two-function list across that boundary to satisfy a
rule about API contact would be worse than the impurity.

**`RowUnregisterAll` (`0x0041DB20`) is reconstructed, and the corrected rule**`RowUnregisterAll` (`0x0041DB20`) is reconstructed, and the corrected rule
worked.** Three of its four callers are inside `ADDR_ROW_UPDATE`, which has 37
call sites and runs whenever a row joins or leaves the map's cell lists -- so
the condition was checked before the work, not after. Its counter reads **74**
on a Boot Camp mission, with `ListUnlink` at 38 on the same run.

It takes a row out of every cell list it is linked into. Three guards first,
and the middle one is the one worth knowing: the FIRST entry's cell index
decides whether the row counts as linked at all, and a negative there means
nothing happens -- not even the dirty mark. The dirty rectangle is collected
BEFORE anything is unlinked, so the row still knows where it was and the region
it occupied gets repainted.

Both the entry count and the buffer pointer are re-read from the row on every
iteration rather than held, and the loop RETURNS on a negative index rather
than skipping it. Reproduced as written: an unlink that shortened the row would
otherwise be a use-after-free, and it is not this function's business to decide
that cannot happen.

**`CommDrainMsgs` (`0x00402690`) is reconstructed, and I picked it wrongly.****`CommDrainMsgs` (`0x00402690`) is reconstructed, and I picked it wrongly.**
The rule from three commits ago -- pick a seam whose caller has a non-zero
counter -- was applied to "called once a frame from FramePre", which is what
that call site looks like at a glance. It is gated on `CommActive()`, on the
same line, and that reads the same field the function itself re-tests.

Measured after the fact: `MsgListRemHead`, which the drain would call at least
once per frame, reads **0** over a whole Boot Camp mission, and `MsgListInit`
reads 6 -- setup only. So the field is clear in single player and `FramePre`
never calls this. Unexercised, verified by reading, and the clean A/B says
nothing about it. **A caller's counter is only evidence if you read the
CONDITION it calls under.**

The reconstruction itself is worth having: the loop re-reads the head after
each node rather than snapshotting, so a handler that queues more work is
drained in the same pass, and `0x00410090` is named from its own strings --
the DirectPlay SYSTEM handler, with `DPSYS_HOST`, `CreatePlayer`,
`DestroyPlayer`, `SESSIONLOST` and `UnHandled System Message` as its cases.

One asymmetry reproduced deliberately: the original pushes FOUR arguments for
both branches, but the game-message half only reads two -- a message pointer
and a dpid, exactly the signature `CommDispatchMessage` was already
reconstructed with. The extra pair is dead on that path, so it is not passed;
under cdecl the caller cleans, and a callee cannot observe arguments it does
not read. Both callees were checked by prologue rather than assumed.

**The death sequence is complete:**The death sequence is complete: `SendDeathMessage` (`0x0042A930`) and
`ObjDeathCleanup` (`0x00428070`) finish it.** Both turn out to be entirely
MULTIPLAYER bookkeeping -- a 16-byte type-0x23 packet in one, and two delayed
events scheduled 3 seconds and 5 minutes out in the other, each behind a bit of
`ADDR_GAME_OVER_FLAGS`. Both sit behind `ADDR_MP_SESSION`, so in single player
they return almost immediately, which is what all six of their Boot Camp calls
do. That early arm is what the A/B compares; everything past the gate is
verified by reading, and the source says so.

**Grepping first prevented a wrong reading.** `obj->[0x544] != 7` looked like an
AI-mode test, because CLAUDE.md records the AI modes as attack 6 and DEFEND 7.
It is not: `army.cpp` already had that offset as `OBJ_OFF_MP_ROLE` with "7 is
the one value anything tests for", and the AI mode is at `+0xE4` per
`ADDR_EVT_SET_AI_MODE`. A plausible connection to a fact already in the file,
and wrong. The macro is promoted to `orig.h` so both modules share one
definition rather than drifting apart.

**And the same `const` mistake twice in one session.** `g_gameOverFlags` and
`g_mpSession` both went in as `const` where the existing definitions are not,
and `checkglobals` refused both. Matching an existing `g_` spelling means
matching it EXACTLY, qualifiers included -- the ratchet is not comparing
addresses, it is comparing expansions.

**`ObjEventMask` (`0x00427D40`, fifteen callers) is reconstructed****`ObjEventMask` (`0x00427D40`, fifteen callers) is reconstructed** -- the top
bit always, one more for the owner's ARMY, then overlapping bits per type
property. The six type tests are independent `if`s and their bits deliberately
overlap, so an ordinary type 2 accumulates two of them; collapsing that into a
switch would change the answer. Its counter reads 1 from an original caller on
top of six calls our own notifiers make that the counter cannot see.

**And a claim from last commit is now measured rather than inferred.** I wrote
that the death sequence runs "since 1000 is lethal" -- an inference, and the
kind this project is supposed to distrust, especially with a
"DamageTrooper: droping armor" message hinting that armour absorbs. A probe on
`TriggerItemDestroyed` settles it: six calls, every one a type 2 already at
zero health. The inference was right, and it is now a measurement.

The same probe explains `DeselectUnit=0` exactly. The dying troopers' flags
read 0 and 0x800 and never 0x400, so none of them was the selected unit and
that arm is simply not reached -- rather than the function being broken or the
death path not running.

**`TriggerItemDestroyed` (`0x00427FD0`) and `DeselectUnit` (`0x00427C80`) are**`TriggerItemDestroyed` (`0x00427FD0`) and `DeselectUnit` (`0x00427C80`) are
reconstructed**, which takes the death sequence to three of its five steps.
The first is event kind 4 and the third member of the 4/5/6 notifier family;
the second is the exact counterpart of `SelectUnit` and lives beside it,
reusing the same `rec` idiom for the selection list. Two of its details are
kept rather than tidied: the loop does not advance after a removal, and
`OBJ_FLAG_SELECTED` is cleared twice.

**A tool of mine was silently wrong and it cost two real names.** The sweep for
pushed string literals -- the cheap antidote this project recommends before
inventing a name -- required every byte in 32..127, so it rejected any string
ending in a NEWLINE. That is what every log message in this image is. It had
reported all ten damage-family functions as naming nothing.

Re-run correctly it recovers `TriggerItemDestroyed` and `Send Death Message`,
and independently **confirms `ADDR_DAMAGE_TROOPER`** -- which had been derived
from a jump-table index alone and turns out to log "DamageTrooper: droping
armor uid:%x". So `ADDR_ON_OBJ_DIED` and `ADDR_KILL_BROADCAST`, both invented
last commit, are renamed to what the program calls them.

**And the wrong name would have survived the A/B.** The log line is gated on
`COMM_OFF_VERBOSE`, which no configuration in the suite sets, so an invented
message there never prints and never diverges. A wrong string behind a debug
flag is invisible to every check this project has -- which is the argument for
taking the literal off the image rather than writing one that reads plausibly.

**`NotifyDamaged` (`0x00427E10`) is reconstructed****`NotifyDamaged` (`0x00427E10`) is reconstructed** -- event kind 5, the exact
mirror of the kind-6 heal notify, and both call sites are inside `DamageObject`.
Each party contributes a triple to `EventNotify`: its `num1`, its uid, and its
event mask from `ADDR_OBJ_EVENT_MASK` (`0x00427D40`, fifteen callers), whose
name is grounded by `event.h` already calling those parameters masks. A null
attacker leaves the second triple as zeros, which the original arranges by
pushing them BEFORE the branch so both arms share them; written here as the
`else` it is.

**Its coverage needed no new run.** All six of `DamageObject`'s Boot Camp calls
take the main path, which reaches this unconditionally -- so it runs six times,
and every one has attacker uid 0, so the NULL arm runs and the two-party arm
does not. Transitivity from a probe already taken, which is cheaper than
another probe and just as good.

One naming call worth recording: the `num1` offset is `0x0C`, and `orig.h`
already has `OBJ_OFF_BOUNDS` there for a different structure. A second
`OBJ_OFF_` name on that offset would be a family alias, so the constant is a
local in `item.cpp` instead. The ratchet is right to refuse it and the fix is
not to raise the baseline.

**`DamageObject` (`0x00428140`, 560 bytes, NINETEEN callers) is reconstructed,**`DamageObject` (`0x00428140`, 560 bytes, NINETEEN callers) is reconstructed,
and it is the best-covered thing this session.** It was `ADDR_GUARDED_ACTION`,
a name this file admitted was a role. The body settles it: a jump table at
`0x0042834C` dispatches on object type to a per-type damage handler, then it
notifies, broadcasts through the already-named `ADDR_DAMAGE_BROADCAST`, and
runs the death sequence when the health it just reduced has hit zero.

Two independent confirmations arrived without being looked for. One of the
nineteen callers is the **"suicide kings"** cheat, which sets health to 1 and
then calls this. And `army.cpp` -- already reconstructed -- calls it twice,
passing `1` for the sixth argument on the branch that has ALREADY broadcast and
`0` on the branch that has not, which is exactly what that argument does.

Ten names came with it and all ten are OURS -- none of the functions names
itself in a string, which was checked first. The four per-type handlers are the
safest, because their evidence is a jump table INDEX rather than a call site:
item, trooper, vehicle, roach for types 1, 2, 3 and 8, with types 4 to 7 having
no handler at all.

**Measured coverage, not assumed.** The counter is blind -- both live callers
are ours now -- so a probe: six calls in a Boot Camp mission, every one type 2,
amount 1000 against 30 or 60 health, attacker uid 0, suppress 0. The trooper
arm runs and so does the entire death sequence, since 1000 is lethal. What does
NOT run: the item, vehicle and roach arms, the 4-to-7 fall-through, the early
already-dead arm, and every multiplayer branch. The source says so.

Closing the seams broke `selftest-link`, which is exactly the failure that
check was written for -- an address call became a real symbol, so `SELFTEST_SRC`
needed `army.cpp`. It said so in its own message and the fix was one word.

**`HealObject` (`0x00428370`, 224 bytes) is reconstructed, and it RUNS.****`HealObject` (`0x00428370`, 224 bytes) is reconstructed, and it RUNS.** It
heals by a percentage of maximum health, clamped to 0..100 and then to the
maximum, and never touches anything already at or below zero -- healing does
not resurrect. An item ignores the percentage and goes to full, by one of two
arms chosen on `OBJ_OFF_REPAIR_FRAME`. What settled the reading was one of the
eight callers: the per-object callback of the **"doctor doctor"** cheat, whose
message is "Avoid the agony...".

**The seam rule needed refining, and the refinement is checkable up front.** A
seam guarantees our code CONTAINS the call, not that our code RUNS. This one
sits behind `EvtObjSet`, a script action handler -- so the question is whether
that handler fires, and its counter answers before any work starts.
`EvtObjSet=1` on Boot Camp, so the path is live; `MovieOpen=2` was the same
check for the last one. **Pick a seam whose caller has a non-zero counter.**

`HealObject`'s own counter reads 0 and cannot do otherwise: closing the seam --
which `checkseams` requires -- made its one live caller ours, calling by name.
A temporary probe resolved it rather than a guess: one call, a real object,
`pct = 100`. So the non-item path runs and that is all; the arithmetic below
100, both clamps and both item arms are reached by nothing here.

**The alias ratchet caught me for the sixth time.** `0x00428370` already had a
name -- `ADDR_OBJ_SET`, and it was in the seam listing I chose the target
from -- and I added `ADDR_HEAL_OBJECT` beside it anyway. Renamed, not aliased:
`OBJ_SET` came off the call site `EvtObjSet`, `HEAL_OBJECT` comes off the body.
`MAX_ALIASES` stays 21.

**`MovieMakeSurface` (`0x00445690`, 32 bytes) is reconstructed, and this one IS**`MovieMakeSurface` (`0x00445690`, 32 bytes) is reconstructed, and this one IS
verified.** It was picked deliberately: after three commits of unexercised
code, the `orig_` seams are a list of calls from our own code into the
original, so anything reachable by a caller we already own is guaranteed to
run. This one is on the intro path.

The function takes a width and a height -- the original's caller computes them
from the film's source rectangle, pushes both, and this cleans 8 bytes for them
-- and then IGNORES them, making a fixed 640x480 surface every time. The
arguments stay in the signature because the caller really passes them; the
discard is the original's.

Verified by watching the film render in full colour through our surface, with
`MovieOpen` at 2 and `MoviePoll` past 970,000. Then the mutation that tests the
claim directly -- use `w` and `h` instead of the literals -- and it changed
NOTHING. A probe says why: the caller passes 640 by 480 both times. The shipped
intro is already full-screen, so the discard is real code with no observable
consequence on any drive here. Said in the source as "would" rather than
"does".

**The next move is still a mission with squadmates**, which would light up
`FormationPoint`, `ResolveFormationPoint`, `ObjAnchorPoint` and `0x00404730`
together. Failing that, the seam list is the place to pick from: every entry is
a call our own code already makes, so its coverage is known before the work
starts. `ADDR_MISSION_NETWORKED` (`0x00421800`, 144 B) is read and understood
-- it decides won-or-lost from the two players' team fields -- but it is
multiplayer mission end, so it would be another unexercised one.

Worth saying why this is the recommendation rather than "write the function":
the name `ADDR_ROW_UNREGISTER` was wrong for 37 callers until it was read, and
`TakeOffMap` was reconstructed on top of it. A second function built on the
same misunderstanding would compile, pass, and be wrong in the same invisible
way.

What is still true of the rest: `0x00449570`, `0x00405050` and `0x004582F0`
would each need a dozen invented names -- `AM2_Object` fields at `0xB0`,
`0xE4`, `0xEC`, `0xF4`, `0x104`, `0x544`, `0x568`, plus tables at `0x00473DD0`,
`0x00475198`, `0x0050712C` and `0x00659F00`. The `obj+0x10` result is the model
for how to unblock them: find the function whose PARAMETER is already
documented, then let its callers tell you what the field is.

## Measured

| | | how |
|---|---:|---|
| `patch_replace` sites | 821 | `grep -rho patch_replace src/game \| wc -l` |
| distinct addresses reconstructed | 821 | each patched exactly once |
| sub-CRT functions in the image | 1,239 | `docs/functions.tsv` |
| sub-CRT code reconstructed | 150,240 / 372,816 B (**40.3%**) | `tools/reconstructed.py`, split at referenced starts |
| the same, crediting whole entries | 164,000 / 372,816 B (44.0%) | what every earlier session quoted, and an over-count |
| modules | 30 flat + 16 `win32/` | `tools/checkclaims.py` |
| pure unreconstructed leaves | **0** (2 listed, both false positives) |
| self-naming unreconstructed functions | 109 at the sweep, 10 taken since | `tools/vectors.py --all` |
| boundary functions reconstructed | 78, 192 import sites | `docs/boundary.md` |
| COM dispatch outstanding | 0 of 79 functions | `docs/boundary.md` |

Read the percentage as what still crosses an original boundary, not as how
much of the game runs on our code -- the count-of-0 blind spot cuts the other
way, and `tools/blindspots.py` says which counters can move at all.

## Verification state

| check | when | result |
|---|---|---|
| `make` | current | builds clean |
| `make check` (16 static checks) | current | all pass, generated files regenerate identically |
| `make selftest` | current | **6,852** DISTINCT vectors, 15,228 words, 13,956 lines, 9,062 spine, 198 variable -- 0 fail |
| `tools/ab.sh campaign` | current | clean, three times: log identical at 14 messages, 2,571/786,432 pixels every time |
| savegame oracle, per section | current | `map` `pad` `script` `eventblock` `event` `air` `audio` **0**; `objscript` 376, all inside pointer fields; `conds` 372, a uniform -196 uid shift; `item` 16 heap pointers; `gameproc` 2 volatile |
| `tools/objdump.py --leader` | current | max health 140, current 140 -- identical to `AM2_NOPATCH=1` |
| `AM2_SELFCHECK=1` | current | 6,144 calls across 48 functions, 0 disagree -- and the pointer arguments finally differ from one another |
| `tools/maskdump.py` | current | roach 32 records/237 points, vehicle 192/3,081, 36,768 bytes, sha256 `532e52a0...` -- byte-identical to `AM2_NOPATCH=1` |
| `tools/anicheck.py` | current | 20 `.ani` files parsed to their last byte, 21 tables in the game, 0 mismatched, 121 borrowed entries all resolved right |
| `tools/ab.sh bootcamp\|windowed\|intro\|audio\|mission\|quit` | not since this run began | the rest of `ab.sh all` is still owed |

A clean A/B is not evidence about a function the run never calls. Check with a
counts probe before reading one as coverage -- that is what turned the
`EvtSetByte530` result from "verified" into "verified by reading", above.

## The menu widget layer, and how well it is covered

1. **The menu widget layer is the current front and the best-verified part of
   the tree.** All five vtable slots have a reconstructed base, plus the
   placement helper, the constructors, the destructors, the focus walkers, the
   shared painter and the forwarding thunks. Three whole subclasses are done:
   the plain label, the focus-highlighting label, and the button (paint and
   mouse update). The edit box's lifecycle is done -- it is the owner of
   `g_charHandler`, and typing into it is checked end to end.

   Two configurations cover it: `tools/ab.sh controls` compares three frames,
   and `tools/ab.sh multi` is the only one that reaches the edit box.

   What is left, smallest first from the ranking in the Leads: `0x00454B70`
   (~43 B), `0x00456D00` (~61), `0x00455110` (~99), `0x00454A10` (~118),
   `0x004561C0` (~122), `0x00456C80` (~124), `0x00455070` (~138). Then the 33
   per-class constructors, which is where the subclass tails get their
   meaning, and the edit box's own painter at `0x00454D20` (242 B, the one
   with the text buffer and the caret).

   **Before claiming any of them, run the address past `orig.h` AND the
   harness patch list.** `0x0045CAA0` looked like an empty virtual and was the
   game's logger; see CLAUDE.md.

2. **Drive a LOAD -- a genuine puzzle, with the ruled-out branches named.**
   `LoadGame` (`0x00425A10`) is reconstructed, patched and traced, and it still
   never runs. Measured, from a temporary `hooklog` probe plus the trace log:

   - The GAME SELECT PANEL's LOAD arm (`0x00452060`, reconstructed) fires: `0x00511B88`
     holds `"map1_mission1.sav"`, `0x00511A68` holds `"sarge"`.
   - Mission start (`0x00425300`) takes the LOAD branch, so `0x00511DD8` was
     set when it read it at `0x00425360`. That global is `ADDR_LOAD_PENDING`.
   - `0x00425950` **succeeds** -- `SetGameDir("save\sarge")`,
     `CheckSaveTag(fp, 0x06660666, gameproc.cpp, 0x528)`,
     `LoadGameProcSection` returning 1. So the flag is NOT cleared at
     `0x00425373`, which was one of the two candidates.
   - `LoadGame` is patched and never traced; `LoadLevelScript` is, so
     `0x004255CB` read the flag as 0.
   - There is **no write** to `0x00511DD8` between `0x00425385` and
     `0x004255CB`.

   The most likely remaining reading is that mission start is ENTERED TWICE
   and the second entry clears the flag before reaching the test, which a
   probe on the two writes would settle in one run.

3. **Fold the pointer-aware savefile comparison into a tool.** Done by hand
   for objscript -- walk the section, collect the offsets holding heap
   pointers, compare everything else -- and it turned "188 differing bytes"
   into a clean result with a sharp pass criterion. `tools/actdiff.py` already
   renumbers pointers by first-seen index; the savefile deserves the same, and
   then `tools/ab.sh` could carry it as a standing check.

4. **Work off the `checkglobals` backlog**, currently 28 surplus names and 15
   surplus spellings. The three worst were the back buffer, the draw target
   and the primary surface, and all three are done. `ADDR_HWND` through three
   names is next, one of which is `g_enumContext`.

5. Keep taking self-naming functions from `docs/logs.tsv`, recomputed against
   the current patch list rather than quoted from an old sweep. The air.cpp
   message list is the notable remainder -- `RemHead` (`0x004010C0`, 144 B,
   10 callers) and `RemMsg` (`0x00401410`, 176 B, 3) -- and CLAUDE.md warns
   it is mutex-guarded and multi-threaded, so a mistake there is a race
   rather than a crash.

6. `tools/ab.sh all` is clean on all eight configurations; see Leads.

## Leads

- **`| head` in a build command hides the exit status, and an A/B ran against a
  stale binary because of it.** `make -s 2>&1 | head -6 && make check && ab.sh`
  continues past a failed compile, because the pipeline's status is `head`'s.
  The compile had failed on a missing include; `make check` then passed on the
  PREVIOUS object files and the A/B measured a binary without the change in it.

  Caught by reading the task output rather than the summary line. Put the build
  in its own command, or check `${PIPESTATUS[0]}` -- never `&&` after a pipe.

- **`ab.sh mission`'s frame guard fired for real**, reporting 0/0 markers and
  refusing to compare "the two ways of not getting there". The re-run gave
  7807/6713 and was clean. That guard was added after a run compared 24,914
  lines against 21,741; this is the first time it has caught a drive that
  reached nothing at all.

- **Six self-naming functions are left in the whole image**, and the sweep now
  covers any size: `ExitAllFromVehicle` (368 B, taken), "Options changed by
  host." (544), `UpdateTrooperAction` (2,080), "Player %s has left the game -
  now AI controlled." (2,256), "Avoid the agony..." (2,304) and "Starting Slave
  Session" (3,040). After that, naming has to come from somewhere else again.

- **Three of `ExitAllFromVehicle`'s callees are named from that one call
  site**, which is the naming this project keeps getting bitten by. Said so in
  `orig.h` beside them: what is evidenced is only what the caller does with
  each answer, and the bodies want reading before the names are trusted.

- **The self-naming pool is nearly dry below 320 bytes.** A sweep for
  unpatched sub-CRT functions that push a string looking like their own name
  turns up exactly ONE: `0x0044C250`, which logs "Trooper Fire Send, trooper:
  %d,  face:%d, pos (%d,%d,%d), loctarg %x, globTarg %x, weap %d, seq:%d". That
  one line named the function and nine of its fields.

  It is a 28-byte army message of kind 0x17 and it cannot run here: no
  DirectPlay session, so it returns at its first test. Verified by reading, and
  the counter exists -- so that 0 is real rather than a missing patch, which is
  a distinction this tree could not draw two days ago.

- **The sequence number is READ and not bumped.** `TrooperFireSend` takes it
  off the flow record at +0x94 and stores it on the trooper at +0x5CC; whatever
  advances it is somewhere else.

- **A function that does not appear in `counts` at ALL has no patch**, and
  that is a usable check only since the trace table stopped overflowing.
  `ProgressBar` was written, compiled, and never installed -- an edit that
  targeted `    patch_replace(...` where the file says `    rc |= patch_replace(...`
  simply did not apply, and nothing static could know a patch had been
  intended. `counts ProgressBar` answering "(nothing traced)" is what caught
  it, three commits after "(nothing traced)" stopped being ambiguous.

- **And the 76 was noise.** That same run put `bootcamp` at 76 differing
  pixels where it has read 22 all session, which looked like the new drawing
  code. Two re-runs on the fixed build: 22 and 22. `ab.sh` has said to re-run a
  difference before believing it since long before this session.

- **`TextExtent`'s height does not depend on the string.** It is the second
  uint16 of the SPACE glyph, read through a fixed entry of the font's offset
  table, so an empty string still answers a line height. The width skips '^' as
  an escape and anything below 0x1F as a control -- and the test is SIGNED, so
  0x80 and up are skipped too.

- **The heading arithmetic exists three times in the image**, which is how it
  was recognised: `AngleBetween` from two points, `AngleOfDelta` from the
  deltas already subtracted, and `DistAndAngle`, which answers the distance and
  the heading together through two out-pointers -- and whose distance half is
  `ApproxDist`'s formula to the instruction. One copy here, called three times.

  `AngleOfDelta` reads **58** in a Boot Camp mission where `AngleBetween` reads
  0, so the shared body IS exercised, just not through the entry that looked
  like the main one. It is also the one of the three the selfcheck can drive
  directly: its arguments are scalars, so `pick` varies them without going
  through the scratch at all.

- **`ADDR_REFRESH_GATE`'s "stays original" meant "not yet".** No reason was
  written beside it and the body is two stores to globals that were already
  named. Reconstructed, and surface.cpp's seam closed with it. A decline with
  no reason recorded is not a decision -- that is what the note beside it is
  for.

- **The voice lines are a table with the answers in it.** `SpeakLine`
  (0x0040BFF0, 35 callers) picks one of a group's wave names at random and
  only when the owner is ours; the groups are 20-byte records at 0x00474444 --
  a count and up to four names -- and the names say what they are:
  Aerosol.wav, AirStrike.wav, AutoRifle.wav, Bazooka.wav, Disguise.wav,
  Explosives.wav. It goes out on slot 0x10, which orig.h already records as a
  voice slot.

  **It read 0, and so did PlayDynamicSound underneath it**, even with the
  ALSA null device attached and both Boot Camp dialogs cleared -- so the whole
  dynamic-sound path was unreached by any configuration here.

  **`ab.sh audiovol` reaches it now**, through a route that needs no mission
  at all: the AUDIO dialog's VOICE bar demonstrates its setting by speaking
  one line. The counters still read 0, because our handler calls SpeakLine by
  name; the evidence is the LCG at `0x0048CC1C` advancing four steps across
  one click on that bar and none across a click on the effects bar.

- **`ListAdd` reallocs per entry.** Every append grows the array to exactly
  count+1 rows of 0x104, so filling a list of n costs n reallocs and n copies
  of everything before it, and the name is copied with no bound at all -- a
  name of 0x100 runs into the value beside it. Both are the original's.

- **`AM2_SELFCHECK=1` was comparing every pointer-argument function against
  ONE input, and the two pointers at the same bytes.** Two defects in one
  line. `fill_scratch` did not vary with the iteration, so all 128 calls saw
  the same memory; and `(i * 7 + 13) & 0xFF` has period 256 while the pointer
  arguments are 0x100 apart, so every pointer pointed at IDENTICAL bytes.
  ApproxDist, PointInRect, PointsEqual and every Obj* predicate were being
  handed two copies of one value, 128 times.

  It is the offline harness's own bug, which `vectors.py` hit with a stride of
  0x800 and fixed with a salt; the in-process one had it with 0x100 and nobody
  had looked. Found by mutating `AngleBetween`'s table index by one and
  watching the run pass -- twice, because the first fix (vary with `k`) was not
  enough.

  Fixed and re-run: **0 disagree**, so nothing was relying on the accident. The
  same mutation now fails 127 of 128 with the arguments printed.

- **`AngleBetween` needed the trig tables seeded to be checkable at all.** It
  reads the two reverse tables, which are .bss zeros when the selfcheck runs --
  every index would answer 0 and the indexing, which is the whole function,
  would go unchecked. `fill_atan_tables()` puts a position-dependent byte in
  each of the 2,050 entries first; the game overwrites both at startup.

  Worth the trouble because nothing else reaches it: twenty callers in the
  image, none reconstructed, and the counter reads 0 through a live Boot Camp
  mission with both dialogs cleared and the view scrolled.

- **The selfcheck learned `byte_ret` too**, for the same reason the vectors
  did: `AngleBetween` returns in AL and the two sides leave different rubbish
  above it -- the division's quotient in the original, a sign-extended table
  byte in ours. `-> ffffff91, original 00000091` was the first thing the fixed
  harness said.

- **The packed key addresses SPRITES.** `PreloadSpriteByKey` (0x00445AD0)
  splits a key into PackKey's three fields and passes them as PreloadSprite's
  first three arguments -- the shifts and masks are `KeyFieldA`, `KeyFieldB`
  and `KeyFieldC` written out, and they agree exactly. Together with
  `KeyLookup`'s table two commits ago, `packkey.cpp` is no longer a set of
  accessors with nothing that uses them. It runs 3,073 times a mission.

- **The state machine's two halves are ours**: `RequestState` raises the
  pending flag and records what is wanted, `CommitState` takes it and moves it
  into `ADDR_GAME_STATE`. CommitState runs whether anything was pending or
  not -- called with nothing wanted it would put -1 into the game state, and
  its one caller checks first.

- **The Boot Camp map is 256 x 256 tiles and 4096 x 4096 pixels**, read out of
  the running game -- `0x00514DD0`/`0x00514DD4` are 4096 and
  `0x00514DDC`/`0x00514DE0` are 256, which also fixes the tile at 16 pixels
  and matches `TileOfPoint`'s shift of four exactly.

  **That is why the map-dimension contradiction has survived: the two globals
  are EQUAL.** CLAUDE.md records `0x00514DDC` as both `ADDR_MAP_TILES_W` and
  `ADDR_MAP_HEIGHT` and says both pairs cannot be right. `TileOfPoint` uses
  `0x00514DDC` as its row stride and `ScriptPad` uses `0x00514DE0` as its row
  stride, which is two pieces of CODE disagreeing rather than two names -- and
  swapping `TileOfPoint`'s stride to the other global leaves `bootcamp`
  identical at 22 pixels, because on a square map they are interchangeable.

  To settle it, read both globals on a NON-SQUARE map. The campaign drive is
  the obvious place and it died twice under the hand-driven sequence; `ab.sh
  campaign` is the reliable route and would need a dump added to it.

- **`TileOfPoint` runs 24,884 times a mission**, which is the most of anything
  taken this session apart from the keyboard pair.

- **The trace table had been silently full, and a full table reads exactly
  like a missing patch.** 104 functions could not be wrapped -- everything
  patched late in `install()`, so the palette, sprite, surface, device and
  winmain modules -- and `counts NearestPal` answered "(nothing traced)",
  which is indistinguishable from never installed. Nothing was WRONG:
  `trace_wrap` falls back to the unwrapped function and the patch goes in
  either way. Only the measurement was gone, and only under `TRACE=1`.

  Third overflow, and `trace.c`'s own comment had already said what to do
  ("the limits want raising, not the message suppressing"). Raised to 2,048
  with the arena sized from it, and the overflow is COUNTED now: `counts`
  appends `[N function(s) NOT WRAPPED: trace table full]`, so the quiet
  version of this failure cannot happen again. `NearestPalIndex` reads 3,072
  and `NearestPalIndexRGB` 50 -- both invisible an hour ago.

- **The unit types name themselves, and the table is 12 records of 40 bytes.**
  `0x004878B8` holds {value, bit, isTrooper, isVehicle, index, char name[20]}
  and the names are IN it: bazookaman, mortarman, grenadier, flamerman, tank,
  jeep, halftrack, truck, ptboat, riflepill, bazookapill, mgpill. That identity
  is certain.

  What the first field means is NOT. It runs 100 to 500, `0x0043A5E0` is its
  only reader, and the three callers are the mission-start screens -- which is
  what suggests a cost and is not proof of one. Named `UnitTypeCost` with that
  said in the header rather than left to look established.

- **`ResetDrawCounts` is blind by construction and stays verified by reading.**
  Its only caller is `ComposeFrame`, which is ours, so closing the seam took
  its counter to 0. It is three stores of zero; the proportionate check is
  reading them, and that is what it has.

- **The keyboard four are all ours now**: `IsKeyDown` and `KeyChanged` off the
  two poll buffers, `KeyPressed` off the edge-and-auto-repeat array at
  0x00512BD0, and `LatchKeyState`, which COPIES the current buffer over the
  previous one so every edge test that follows sees no change. Note it copies
  where `PollKeyboard` swaps -- two different operations on the same pair.

- **`FreeAllFonts` is teardown entry 7**, so six of the thirteen are named now.
  It reads 1 on the quit path and `FreeFont` reads 0, which is the ordinary
  blind spot: the sweep calls it directly.

- **`ListFirstField548` runs 348 times a mission** and `SetLeadsAndAct` once.
  Between them they are the only writer and one of the readers of the dword at
  +0x548, whose meaning is still not established -- `LookupOwnerObj` picks an
  army's object by it and this puts 1 in it. Worth knowing that the whole set
  of things that touch it is now small and reconstructed.

- **The packed key has a TABLE, and it is what the region pass consults.**
  `0x00434290` binary-searches a sorted {key, value} array at `0x00516150` and
  `0x004346E0` packs its three arguments with exactly `PackKey`'s arithmetic
  before handing them over -- which is what ties `packkey.cpp`'s field
  accessors to something that uses them.

  Checked by mutation rather than by a clean run: `KeyLookup` runs 1,588 times
  in a Boot Camp mission, and making the search never match puts the frame
  293,671 pixels wrong -- 37% of it -- and drops the game's own "calculating
  region data..." line. That log line is also the only evidence so far of what
  the table is FOR.

- **`ab.sh controls`' pixel figure is NOT deterministic, and this file said it
  was.** Four runs of one build gave 0/0/0, 45/45/45, 54/45/50 and 0/0/0 --
  the key-capture boxes blink, so two screenshots agree only when they land in
  the same phase. The widget tree (25 nodes) and the log (5 messages) are
  identical every time, and those are the evidence. A 0 there is luck, not a
  guarantee; the earlier "0 pixels on all three frames" was one sample read as
  a property.

- **`orig_` was one spelling of the seam and not the only one.**
  `frame.cpp` reached the movie step as `call0(ADDR_MOVIE_FRAME_STEP)`, which
  was fine until `0x00445630` was reconstructed and then went through the
  detour into our own code -- exactly what `checkseams.py` exists to stop,
  written differently. It checks `callN(ADDR_X)` at a call site now as well as
  `#define orig_x ... ADDR_X`, and is tested in the failing direction by
  putting the call back.

- **And that made `blindspots.py` wrong in the other direction.** It reported
  `MovieStepCurrent` blind because both its callers are reconstructed, while
  the counter read **746,792** -- because those callers were reaching it by
  ADDRESS. Closing the seam took the counter to 0 with no behaviour change at
  all: `MoviePoll` still reads 712,509 on the same run. A counter falling to
  zero for that reason is the blind spot happening on purpose, and it is worth
  having seen it once.

- **`IsKeyDown` and `KeyChanged` are the two most-called reconstructions in
  the tree**: 2,669,477 and 2,667,117 in one Boot Camp mission. Both mask the
  scancode themselves and read through `ADDR_KEYS_NOW_PTR`, because
  `PollKeyboard` swaps the two buffers each poll. `IsKeyDown` returns 0x80
  rather than 1 -- an `and eax, 0x80` with no normalisation -- so every caller
  is testing it against zero.

  Neither can go in `AM2_SELFCHECK=1`, and for the reason `LookupOwnerObj`
  taught last commit: the buffer pointers are NULL before `install()` runs.

- **`Cos8` and `Sin8` return a FLOAT**, in st(0), which puts them outside both
  differential harnesses -- the vectors and the selfcheck each compare eax.
  What checks them is one layer down: they are the only readers of the two
  forward trig tables, and `tools/trigdump.py` compares those byte for byte.

- **The nine smallest functions left were worth more than their bytes.** 192
  bytes across five modules, and eight of the nine already had a name in
  `orig.h` waiting for them -- `ADDR_MENU_ROW`, `ADDR_CLEAR_PTR_LIST`,
  `ADDR_BUILD_FONT`, `ADDR_OBJ_BY_UID`, `ADDR_MOVIE_CURRENT` and the movie
  block. Grepping the address first turned what would have been nine guesses
  into one: `0x00412DD0` is `GetMenuRow`, `0x0044BA60` and `0x00446830` are
  plain cdecl wrappers, `0x0042A660` is the constructor for the record
  `ADDR_CLEAR_PTR_LIST` empties, and `0x0042A670` is a one-instruction alias
  for that teardown.

- **The movie vtable dispatch works, and its counter proves the chain.**
  `MovieStepCurrent` reaches `MoviePoll` through `object -> table -> slot 0`,
  and on the intro path `MovieStepCurrent` reads 746,792 while `MoviePoll`
  reads 746,794 -- so the two dereferences are right. Writing that as one
  dereference calls the vtable pointer as a function and the game exits
  instantly; CLAUDE.md says it cost an iteration once.

- **`ab.sh` can SEE a defect and still report clean, and here is a measured
  case.** Forcing `SetMaxHealth`'s difficulty index to 0 doubles the player's
  health, and `bootcamp` goes from its usual 22 differing pixels to 96 -- and
  passes, because 96 is well inside the budget of 500. The budget is what
  makes the check survive a moving scene; it is also what makes a small real
  difference invisible. Read the number, not the verdict.

  `tools/objdump.py` is the answer for this class: it reads a registered
  object's fields out of the running game by uid, binary-searching the sorted
  table. The leader's max health is 140 on a correct build and 280 on the
  mutated one -- 4.0 against 2.0, exactly. That turns "verified by reading"
  into a comparison for every function that writes an object field and returns
  nothing, which neither the vector harness nor `AM2_SELFCHECK=1` can check.

- **The game gives up on you gently.** `SetMaxHealth` takes five health off
  every enemy for each retry of the level, divided by 2*difficulty + 2 --
  faster on easy than on normal, and not at all on hard, where the whole enemy
  branch returns without writing. `0x00512330` is the retry counter, and it
  says so: the level loader logs `"Attempt# %d"` and `0x00421890` clears it
  when the campaign moves on.

- **And that rubber band is unobserved in every run here.** Boot Camp reads
  difficulty 1 and attempt 0, so the enemy arm computes
  `max(amount * 0.33, amount - 0)`, which is `amount`. The 0.33 constant, the
  division and the floor are all verified by reading; only the player's x2 and
  the 400 cap are actually exercised.

- **Two names in `orig.h` were wrong about what they named, and both were
  named from a single call site.** `ADDR_SPRITE_DROP_NAMED` (`0x00457820`)
  walks every object an army owns and CALLS its second argument -- `call ebp`
  -- so nothing about it is a sprite; the one call site passes `0x0045A030`,
  which is a function that hands a unit to the AI, matching the "left, AI takes
  over" message beside it. And `EvtMarkSet`/`EvtMarkClear` write into the 4x4
  ALLIANCE matrix: `0x00424E80` fills that table with the identity and then
  allies any two comm players on the same team. They are `ForEachArmyObject`,
  `ADDR_OBJ_TO_AI`, `EvtSetAllied` and `EvtClearAllied` now.

- **And the alias ratchet caught the author again**, one commit after the
  lesson was last written down. Five addresses went into `orig.h` and two
  already had names; the group was grepped for its globals and not for its
  functions. Grep EVERY address, not the ones that feel new.

- **A function can be safe for `AM2_SELFCHECK=1` and still not survive it.**
  `LookupOwnerObj` range-checks its army perfectly and then indexes
  `0x004F9ECC`, which is NULL that early -- the selfcheck runs before
  `install()`, which is before the game has loaded anything. It took the
  process down with 47 functions announced and no summary, the same symptom
  `XorChecksum` produced. The question is not only "does it survive a random
  argument" but "does it survive the empty world this runs in".

- **`ObjIsFriendly` passes the selfcheck and one mutation of it also passes.**
  Inverting the matrix lookup fails all 128 calls, so that half is genuinely
  compared. Changing the `owner == 4` shortcut to `owner == 5` changes
  nothing: the scratch byte at that offset is fixed, so the argument never
  varies. The shortcut and the multiplayer block -- `g_mpSession` is 0 before
  anything loads -- stay verified by reading.

- **The `.ani` subsystem is closed: five loaders, six frees, one table
  reader, three lookups, two mask builders.** Nothing between a `.ani` file on
  disk and a sprite index is original any more.

- **A `chdir` is a side effect, so which side of a test it falls on is
  behaviour.** `LoadExplosionAnims` and `LoadMissileAnims` chdir into
  `data\ani` and THEN test whether the table is already loaded;
  `LoadRoachAnims` tests first and returns without chdiring. Reproduced rather
  than tidied.

- **Neither oracle sees the soldier loader's last line**, which rewrites the
  `next` of rifleman's animation 0x46 to -1. `anicheck.py` reads the table
  inside LoadAnimTable, before the fixup, and `maskdump.py` never looks at the
  soldier tables. Checked by reading the entry over the control socket in both
  builds instead: id 70, `next` -1, on ours and under `AM2_NOPATCH=1`.

- **The boat is given the jeep's turret** and vehicle kind 4 has no paths at
  all -- two of the twelve slots in `LoadVehicleAnims`' table point at the
  image's shared empty string at `0x004F96B8`, which 67 sites use and nothing
  writes. Both reproduced as they stand.

- **The game calls these masks, and it said so itself.** `BuildVehicleMask`
  logs `"vehicle mask direction: %d"` under `-traceVEH`, which named the whole
  family: the tables are MASKS and their index is a DIRECTION. What went in as
  `footprints`/`facings` is renamed throughout -- `AM2_Anim::directions` and
  `directionBits` with it, since that message's counter runs over exactly that
  field. `tools/footprints.py` is `tools/maskdump.py`.

- **The vehicle bases are confirmed by tiling, which is the check that matters
  after the roach's was wrong.** Six turret animation tables end exactly at
  `0x0065A2D8`, six direction counts end exactly at `0x0065A2F0`, and
  6 x 32 records of 0xA4 end exactly at `0x00661DF0`, which is
  `ADDR_VEHICLE_ANIMS`. Nothing left over anywhere.

- **The roach wants 16 of its 64 samples solid and the vehicle 12.** Same
  builder otherwise; not unified, because the two constants are the only thing
  separating them and a shared helper would hide that.

- **`BuildVehicleMask=5`, not 6.** The loader skips a kind whose path is empty,
  so its direction count stays 0 -- `dirs 32,32,32,32,0,32` -- and the mask
  builder is never called for it. The `directions <= 0` early return is
  therefore NOT what that zero comes from; nothing reaches it.

- **The mis-centred trig table happened again, and only a table dump caught
  it.** `BuildRoachFootprints` writes each record's count through `[ebp-4]`
  with `ebp` starting at the POINTS, so the array begins at `0x00654CA8`.
  Taking `0x00654CAC` as the base put the whole table one dword early, over the
  global at `0x00654CA4` -- every point correct and every one in the wrong
  place. `tools/maskdump.py` found it on its first run by hashing the raw
  region rather than the decoded records.

- **And that A/B cannot fail on this at all.** With the sample step doubled
  from 2 to 4, all 32 records change and the point total drops from 237 to 25;
  `ab.sh bootcamp` is still clean at the usual 22 pixels with an identical log.
  So the table is verified by `footprints.py` or by nothing.

- **`0x0043C720` was 12 bytes, not 432.** It is `FreeMissileAnims`, the fifth
  anim sweep, and `docs/functions.tsv` had run it together with the roach
  footprint builder next door. The teardown table's comment said it "does more
  than free" on that basis; a merged entry, exactly as `merges.py` warns.

- **The three animation lookups are what prove anim.h's field names.**
  `0x0044BB30`, `0x0045D9B0` and `0x0045DA20` each find the entry with a fixed
  id -- 1 for soldiers, 0x51 for vehicles and turrets, and the shipped files
  bear both out -- then hand `facingBits` to `RoundTo8` as its BIT COUNT and
  index the cell grid at `frames * facing`. So `facingBits` really is the log
  of the facing count and `frames` really is the inner stride. Nothing in the
  loader could have settled either; only a reader could.

  All three are installed and read 0 in every configuration. The gate is
  `0x004FCF84`, four frames up the chain at `0x00413BC0`, which also rotates a
  ghost with keys 2 and 3 and places it at the cursor -- a developer placement
  overlay, not gameplay. Verified by reading plus the structural agreement.
  Finding the switch that sets `0x004FCF84` would make all three drivable.

- **The turret lookup returns NULL where the other two fall back to entry 0**,
  and it opens with a null test on `lea esi, [eax*8 + 0x65A2A8]` -- the address
  of a global plus an index, which cannot be zero. Not reproduced, same as
  `UpdateMouseState`'s unreachable `je`; the NULL return IS reproduced, since a
  turret with no animation is a different thing from one drawn as its first.

- **Four of ShutdownSubsystems' thirteen teardown entries are no longer
  guesses.** They are the anim sweeps -- explosions, roach, vehicles+turrets,
  soldiers -- each a `push <table>; call FreeAnimTable`, and the table names
  the `.ani`. Measured at shutdown: all four run once and `FreeAnimTable`
  itself reads 1, called with `00654C90`, the missile table, from the one
  caller still original. `ab.sh quit` clean at 8 messages and 0 pixels.

- **`counts` truncates, and the filter argument is the answer.** The three new
  lookups were absent from an unfiltered `counts` reply, which reads exactly
  like "never installed" -- the failure `control.c`'s own comment predicts.
  `drive.sh ctl "counts Anim"` lists them. Use the filter whenever a name you
  expect is missing.

- **The `.ani` format is confirmed by arithmetic.** `LoadAnimTable` --
  `LoadSpriteFile`'s tail, and the last unread part of a sprite file -- reads a
  count and that many 16-byte entries, each either an animation of its own or a
  borrow. Parsing all twenty shipped files with that layout ends every one on
  its last byte, 349 entries in all; `rifleman.ani` is 1,103,262 of 1,103,262.
  A mis-sized field could not do that.

  `tools/anicheck.py` then compares what the game built against that parse.
  Sprite indices are compared as deltas, because the absolute value depends on
  how many sprites earlier files put in the list.

- **Everyone borrows from the rifleman.** Eight soldier files pass rifleman's
  table as their fallback, and an entry with no animation of its own takes the
  one with the same id out of it -- `grenadier.ani` gets 43 of its 49 that way.
  `explosions.ani` passes no fallback at all, so its three borrowers fall to
  the loader's final fixup and take its own entry 0. All 121 resolve to the
  predicted pointer.

  Two paths the shipped data cannot reach: no borrowed id is missing from
  rifleman, so the `entries[0]` last resort never fires, and rifleman's 52 ids
  are distinct, so first-match and last-match are indistinguishable.

- **`facings` is always a power of two, which is what fixed the field names.**
  The loader hands it to `0x0042DFE0`, a jump table that turns a single-bit
  value in 1..0x8000 into its bit index, and stores the answer in a byte. The
  shipped files use 1, 2, 8, 16 and 32 and nothing else. So the pair
  multiplying to the cell count is frames x facings and not two anonymous
  dimensions; the cell data settles the order, stepping consecutively within a
  facing and jumping between them.

- **`LoadSpriteFile` runs 21 times in Boot Camp**, once per `LoadSpriteSet`, so
  both halves of the sprite loader are measured rather than assumed.

- **A byte-returning function needed the vector harness to learn a new
  thing.** `Log2Mask` (`0x0042DFE0`) writes `al` and leaves the rest of `eax`
  holding its own argument -- the recorded answer for `Log2Mask(0)` is
  `0xFFFFFF00`, which is `dec eax` on 0 followed by `xor al, al`. The harness
  compared all 32 bits, so 90-odd vectors failed on register contents the i386
  ABI says nobody may read. Measured, not assumed: turning the new `byte_ret`
  flag off reproduces exactly those failures with the low byte agreeing every
  time. `VOID` was already the same problem one step further on.

- **A 16-way switch is the case for an explicit argument set.** 96 random
  32-bit arguments reached 50.8% of `Log2Mask`, because a random integer is
  almost never an exact power of two. Twenty-five values -- every power, the
  ends of the compare chain, and near-misses for the default arm -- put it at
  100%. Mutating one arm fails 3 vectors and prints the argument.

- **`pad28` was not padding.** `sprite.h` had eight bytes at 0x0028 named as
  filler; `LoadSpriteSet` reads two int16 straight out of the file into 0x0028
  and 0x002A, beside hotX and hotY and in exactly the same shape. They are
  `fileA` and `fileB` now -- what they MEAN is still unknown, but "padding" was
  a claim and it was wrong.

  A field is only padding when something has looked for a writer. Nothing had.

- **A sprite's format comes from the CALLER, not the file.** `LoadSpriteSet`
  sets it to 3 for flag bit 0x10, 2 for bit 0x08, and otherwise leaves the zero
  the memset put there -- which `sprite.h` reads as "image is a DirectDraw
  surface". A set loaded with neither bit would claim to hold surfaces while
  holding file bytes. No caller does it; the zero is left alone.

- **`LoadSpriteSet` runs 21 times in a Boot Camp mission** and feeds the 5,798
  `RemapSpriteRuns` calls, so this A/B is real evidence rather than "nothing
  else broke". Probed rather than assumed, which is the habit the last commit
  argued for.

- **`RemapSpriteRuns`' unused second argument is the image byte count.** The
  caller has it and passes it; the RLE walker does not need it, because the
  header already says how far to go.

- **A counts probe on today's work, and it splits three ways.** Every commit
  since the comm family said "bootcamp and mission clean". One probe says what
  that was worth:

  | | calls | what the zero means |
  |---|---:|---|
  | `RemapSpriteRuns` | **5,798** | genuinely exercised; the A/B is real evidence |
  | `GrowSpriteList` | **58** | the same |
  | `DoAirSupport`, `AirSupportPop` | 0 | REAL zeros -- `blindspots.py` says these counters can move, so the air-support path simply never runs in Boot Camp. Verified by reading |
  | the five `FreeItem` arms, `AirSupportBegin`, `AirSupportClear` | 0 | BLIND -- every caller is ours, so the zero says nothing either way |

  So of eleven functions landed since the comm work, two are exercised, two are
  known not to run, and seven are unmeasurable from the outside. The A/Bs were
  still worth having -- they establish that nothing else broke, which is not
  nothing -- but "clean" was doing more work in those messages than it earned.

- **The `quit` configuration is the one that reaches the free family**, through
  `ReportLeaks`' "Unreleased memory (0) blocks:", and that is why it is worth
  running for anything that frees.

- **The duplicate-PATCH check earned its keep a third time.** I wrote a thunk
  for 0x00409920 that `winmain.cpp` had already reconstructed as
  `FreeSpriteListAlias` -- one of the twelve WinMain-chain functions CLAUDE.md
  names -- and `checkpatches` refused the build. The two before it were
  `TakeUid`/`AllocUid` and `ScriptCompare`.

  What the alias JUMPS to had a name too, `ADDR_FREE_SPRITE_LIST`, reached
  through an `orig_` seam. So this was never new frontier: it was the seam
  under a function that was already ours, and closing it is what the commit
  actually did.

  The lesson is narrower than "grep the address". I DID grep -- for
  0x004098B0, and found nothing, because the constant is spelled
  `0x004098B0u` in one place and the thunk is a different address entirely.
  Grep the address of every function in the group, not just the one you
  started from.

- **Naming follows the code that is already there.** The globals became
  `ADDR_SPRITE_LIST*` rather than my `SPRITE_POOL*` because `orig.h` had
  already committed to "sprite list" in the two function names. A new name
  beside an established vocabulary is a second vocabulary.

- **`GrowSpriteList` does not look at the count.** It adds a hundred to the
  capacity and reallocs, so it is "make room", called by whoever is about to
  need it, rather than "grow if full". And nothing checks the realloc.

- **Three ratchets fired on one 112-byte function, and all three were right.**
  `checkseams` caught a fresh `orig_` on 0x00457420, which `objtype.cpp` has
  had as `ObjIsTypeIn238` for a long time; `checkglobals` caught the game clock
  spelled `(const int32_t *)(uintptr_t)` where `event.cpp` spells it
  `(const uint32_t *)AM2_IMAGE(...)`; and the COMPILER caught `OBJ_OFF_FLAGS`,
  which I put in `orig.h` while `item.cpp` had a local copy of the same offset.

  The third is the one with no tool behind it -- a duplicate `#define` is only
  a warning, and only because both were in scope at once. The offset now lives
  in `orig.h` alone.

- **The "radius" is ApproxDist's, so it is a diamond and not a circle.**
  `TakeNearbyOffMap` measures with `ApproxDist`, which is the game's cheap
  approximation, and that is what decides who gets caught by an air strike.
  Reproduced rather than tidied into a true distance.

- **Three tests in the original's order, all three needed.** Type 2, 3 or 8;
  not ALREADY off the map, which is the 0x0800 flag the taking-off sets; and
  within the radius. The second is what stops a second strike re-scheduling
  something already gone.

- **The air-support family is complete** -- request, enemy check, queue head,
  start and reset. Five functions, and every one of them is inside or beside
  the 584-byte block `air.cpp` was already saving, so the savegame section and
  the live subsystem turned out to be the same thing.

- **The caller's `kind` is a floor, not a decision.** `DoAirSupport` takes kind
  2 as given, but anything else asks `FindEnemyNear` and becomes kind 3 if
  there is one. So a caller cannot ask for the quiet drop when the drop zone is
  contested -- except by asking for kind 2, which skips the check entirely.

- **It calls `AirSupportBegin` with the entry written and the count still
  zero**, so Begin reads a slot the count says is not there. Harmless, because
  Begin only ever looks at slot 0, and it is the original's order.

- **The same field is written as a dword and copied as two words.**
  `DoAirSupport` stores `where` with one 32-bit move; `AirSupportPop` moves its
  two halves separately. Both reproduced, because the packing is only visible
  in the second.

- **CORRECTION: `obj + 0x0010` is the OWNER, not a kind byte.** Two commits ago
  I wrote that `TrooperDropItem` "sets its kind byte to 4, so a dropped item
  becomes a weapon object on the ground". It does not. `objtable.h` has had
  that field named `owner` since long before today, and army 4 is the neutral
  one -- so what dropping an item does is give it to NOBODY, which is what a
  thing lying on the ground should belong to.

  Two independent uses settle it: `TrooperDropItem` passes that byte to
  `CommMustBroadcast`, which takes an army, and `FindEnemyNear` compares it
  against `UidArmy`. I guessed a field the repo had already named -- the same
  failure as naming a function from a call site, one level down.

- **And `OBJ_OFF_OWNER` in `orig.h` is a DIFFERENT structure's field**, at
  0x0004. The object's owner is at 0x0010 and lives in `objtable.h`'s
  `AM2_Object`. Two right names, one collision, and the wrong one is the one
  that greps first.

- **`FindEnemyNear` calls `UidArmy` once per CANDIDATE**, not once before the
  loop, so a query returning forty objects calls it forty times. Reproduced.

- **`0x0042A240` is "every object in a rectangle"**: clip to the map extents,
  shift down by eight into tile coordinates, walk the cells, keep what the
  predicate accepts, and thread the result through 0x0068 of each object.
  Named from the body; three callers.

- **"EndMission" is a log PREFIX, not a function name**, and the self-naming
  sweep would pair it with whichever function it reached first. 0x00408FF0
  prints "EndMission  AirSupport.count decreasing to: %d" and DoAirSupport
  prints "EndMission  AirSupport.count increasing to: %d" from a different
  function entirely -- and DoAirSupport names ITSELF on the line above its own.
  Second time the sweep has been shown to attribute a string wrongly, after
  "TIMING OUT PLAYER"; the first was a merged entry, this one is a shared
  prefix. Read the body.

- **The air-support queue IS the block `air.cpp` saves.** All nine fields are
  offsets into ADDR_AIR_SAVE_BLOCK rather than addresses of their own, and the
  layout closes it exactly: the last flag sits at 0x0244 of 584 bytes. That is
  independent confirmation of both the field map and the block size, from two
  facts that were established years apart in this project.

  It also settled a naming collision the honest way -- `checkpatches` refused a
  second name on 0x004F945C, which is the block's start AND the active flag,
  and expressing the fields as offsets removes the question rather than
  answering it.

- **Two of the three tail into each other.** `AirSupportPop` really does
  `jmp` to Begin or Clear rather than calling them, which is why they are three
  functions and not one with arms. And Begin's two shapes disagree about the
  active flag: only the sound-playing one raises it.

- **"Must I tell the other players?" is one function, and nine callers ask it.**
  `0x0040F560` answers NO when there is no multiplayer session at all -- which
  is what settles the name, because under "is this army mine" a single-player
  game would have to answer yes to everything. Army 4 answers "am I the host".
  Everything else answers "is that slot NOT remote", inheriting
  `CommSlotRemote`'s three-valued oddity: a slot answering -1 is truthy there
  and becomes 0 here.

- **`g_hostChanged` was the multiplayer-session flag under a name from one
  writer.** `OnHostChanged` puts 1 in it, and that is not a second meaning --
  it is a machine that has just become the host asserting there is a session.
  The `orig.h` macro said `ADDR_MP_SESSION` all along and a comment beside the
  local name explained the discrepancy rather than fixing it. `checkglobals`
  refused to let the honest name exist alongside it, which is the ratchet
  working exactly as intended: it does not care which name is right, only that
  there is one.

- **`TrooperDropItem` is read and NOT written**, because it needs five more
  names first: 0x0042B290, 0x00439F40 (352 B), 0x0044C150 (256 B, the send
  side of a trooper message) and 0x00427F60. Reading it did establish that a
  trooper's inventory is six uids at 0x054C and that slot 0 is the wielded
  weapon -- the same field `DestroyTrooper` reads as `TROOPER_OFF_WEAPON_UID`.
  It also sets the dropped item's kind byte to 4, so a dropped item becomes a
  weapon object on the ground.

- **`FreeItem`'s whole switch is closed** -- all five arms, eight kinds, no
  `orig_` left in it. The bare arm serves kinds 1, 5, 6 and 8 and is exactly
  the tail the other four share; every other arm is that plus something.

- **The kind-7 counter is bounded at BOTH ends, which is why the clamp is not
  tidying.** 0x00435550 refuses to make a thirty-third kind-7 object and the
  free clamps the count at zero coming down. Reproducing only one half would
  have looked like a defensive check worth dropping.

- **A comment beside a seam goes stale exactly when the seam closes.**
  `item.cpp` carried "the five per-kind destructors stay original and are
  reached by address" while they stopped being original one arm at a time over
  three commits. Nothing checks prose; `checkseams` only sees `orig_` macros,
  and the last of those went in this commit, taking the sentence's last
  reader with it. Corrected, and the correction says what happened.

- **`FreeItem`'s arms are one function written four times, and the differences
  are the interesting part.** Three are now ours -- trooper, vehicle, weapon --
  and every one ends the same way: free the subrecord rows, hand the object to
  `DestroyItemObject`, free the object.

  What differs: the vehicle keeps its weapon uid at 0x0550 and the trooper at
  0x054C; the vehicle also empties a pointer list at 0x0538 that neither other
  arm has; the trooper's log is behind the verbosity flag, the weapon's is in
  front of it, and the vehicle has none at all. And the same weapon flag is set
  with an 8-bit OR in one arm and a 32-bit OR in another -- the compiler's
  difference, from one piece of source written twice.

- **The COMMON arm at 0x0043BBB0 is the bare version**, 48 bytes, serving kinds
  1, 5, 6 and 8: the shared tail and nothing else. Read, and the obvious next
  step -- with `ADDR_FREE_ITEM_KIND7` after it, that closes the whole switch.

- **Both of these landed as seam closures.** `FreeItem` was already ours and
  was calling into the image for kinds 3 and 4; it now calls functions. That is
  a different kind of progress from a new frontier and the percentage barely
  moves for it -- 96 bytes -- while three `orig_` seams disappear.

- **"Grep the address first" failed again, and this time BOTH ratchets caught
  it.** `0x004478C0` names itself "DestroyTrooper", and I added
  `ADDR_DESTROY_TROOPER` for it -- while `orig.h` already called it
  `ADDR_FREE_ITEM_KIND2` and `item.cpp` already reached it through
  `orig_free_kind2`. `checkpatches` reported the 32nd alias and `checkseams`
  reported the seam in the same run.

  The old name was not wrong either: it is the kind-2 arm of `FreeItem`'s
  switch, and kind 2 is the trooper -- `ReceiveArmyMsg`'s switch says so
  independently. So the address keeps the family name that `FreeItem` reads by,
  and the C++ function takes the name the function gives itself. When two names
  are both right, keep the one the surrounding code is organised around.

- **This landed as a SEAM CLOSURE, not a new frontier.** `FreeItem` was already
  ours and was calling into the image for kind 2; now it calls a function.
  Three of the five arms are still original -- common, kind 3 and kind 4 -- and
  each is a self-contained target of the same shape.

- **A trooper is freed twice over, in an order that matters.**
  `DestroyItemObject` frees the 0x0090 allocation and clears the live byte, and
  only then is the object itself freed. Reproduced in that order.

- **The weapon step is skipped in silence for anything that is not a weapon.**
  `WeaponByUid` complains and answers null for any kind but 4, and a zero uid
  never asks -- both land on the same path, and the trooper still comes down.

- **BOTH dispatchers are ours now.** `CommDispatchMessage` handles the
  packet-level messages and `ReceiveArmyMsg` handles one message out of a
  packet, and the two switch on different things: the first on a message type,
  the second on the object KIND behind the message's uid. 2 is a trooper, 3 a
  vehicle, 4 the game itself, and 1 and 5 are accepted and ignored in silence.

- **A message about uid 0 is attributed to whoever sent it.** Every other
  message is attributed to the uid's owning army; uid 0 belongs to nobody, so
  the packet's sender slot is used instead.

- **Two calls in it are to ADDR_LOG, which this build has stubbed to a single
  `ret`, and one passes the MESSAGE BUFFER where a format string goes.**
  Reproduced through a typed pointer so the compiler has no opinion, because
  what the image does is call a function that ignores everything.

- **GAME_WON is recorded as GAME_LOST unless 0x00512304 is set.** With winning
  enabled the winner is 1 if it was us and the army otherwise; with it clear,
  the win takes the loss arm exactly. Both arms then write the menu request and
  the state-pending flag BY HAND, without going through `RequestState` -- the
  same pair CLAUDE.md records as the route to the level teardown, reached here
  from a network message rather than from ESCAPE.

- **`0x025C` is "this army is in play".** `ArmyIsInPlay` answers yes for army 4
  unconditionally -- the one every colour lookup treats as neutral -- and reads
  that field for anything else. It is the same field `CommSlotRemote` falls
  back to for an empty slot, which now makes sense of that fallback.

- **The comm receive path is complete.** `CommDispatchMessage` and every
  handler it names are reconstructed; what is left below it is
  `0x0040FBB0`, the SECOND dispatcher, which handles one message out of a
  packet and is 736 bytes of item and unit traffic.

- **`ReceivePlayerMsg`'s loop bound is the address of the next global.** It
  fills `ADDR_ARMY_SETTING` at 0x00515FE0 and stops when the cursor reaches
  `ADDR_SCORE_LIMIT` at 0x00515FF0 -- exactly four slots, whatever count the
  message carries. Second instance of that shape in this image.

- **And the bound check sits in the MIDDLE of the loop body.** A fifth record
  still gets its remote flag set and can still overwrite `ADDR_OUR_SLOT` before
  the loop gives up, because both happen before the check. A reconstruction
  that hoisted the test to the top would be tidier and wrong.

- **A record that is not ours has its remote flag set TWICE**, once before the
  bound check and once after -- and only the second is guarded by the player
  count. Both reproduced.

- **The version-mismatch message names OUR player, not the sender's.** "%s has
  a different version of the game" is filled from `ADDR_DEFAULT_OWNER`, so a
  client that disagrees with the host announces itself.

- **`checkglobals` caught a spelling, which is what it is for.**
  `g_defaultOwner` is `uint32_t` in `objtable.h` and I wrote `int32_t`; that is
  one definition of one address becoming two, and the ratchet refused it before
  it could become a habit.

- **`ADDR_HOST_SLOT` was our slot, not the host's**, and the way it went wrong
  is one this file has recorded three times for FUNCTIONS and not once for a
  global: it was named from a call site. `CommOpenSession` reads it, and the
  machine opening a session IS the host, so the wrong reading and the right one
  agree there and nothing looked amiss.

  `0x0040E117` settles it. The player-created handler writes it only when the
  new player's id equals the comm object's own id -- that is us, whoever is
  hosting. `CommRemovePlayer` confirms it by decrementing this and
  `ADDR_DEFAULT_OWNER` together when a lower slot leaves.

  The host-migration message is the one that looks like counter-evidence and is
  not: it fills "Player %s is now the host" from this slot, naming US, which is
  right because that handler runs on the machine that has just taken over.

  Renamed to `ADDR_OUR_SLOT`, and the two modules reading it renamed with it.
  **Before trusting a global's name, find the site that WRITES it**; a reader
  can agree with a wrong name for a reason peculiar to that reader.

- **The two globals track the same value and can still diverge.**
  `ADDR_DEFAULT_OWNER` is read about 130 times across items, units and weapons
  and is written in one place `ADDR_OUR_SLOT` is not (`0x0040EB57`). They are
  not aliases, and neither is redundant.

- **A three-valued query that only sometimes answers the question it is named
  for.** `CommSlotRemote` reads the 0x020C flag only for an OCCUPIED slot; an
  empty slot that once held someone answers "am I NOT the host" from 0x025C
  instead, and anything else answers -1. A caller treating the result as a
  boolean gets a truthy answer from that -1 as well. Three different questions
  behind one return value.

- **It takes its slot as a SIGNED WORD**, alone in the family -- every other
  per-slot accessor takes a full `int32_t`. A negative would index backwards.

- **These three are PURE and could have vectors.** They reach the comm object
  only through `this`, never through the global, so `tools/vectors.py` could
  record them. That is why they went into `msgslot.cpp` rather than
  `commmsg.cpp`: the module split made a fortnight of an hour ago is exactly
  the line "can this be checked offline", and these fall on the offline side.

- **Next up is `ReceivePlayerMsg` (0x004114E0, 928 B), read but not written**,
  and it is worth knowing what it will cost. Its loop bound is taken from the
  ADDRESS OF THE NEXT GLOBAL -- it walks a table at 0x00515FE0 and stops on
  reaching `ADDR_SCORE_LIMIT` at 0x00515FF0, so there are exactly four entries.
  That is the same shape as the registration table walking up to
  `ADDR_SCRIPT_CONDITIONS`.

  It also puts pressure on two existing names. `ADDR_HOST_SLOT` (0x004FA904)
  is set here to the slot whose id is OURS, which is not what "host slot"
  means, and `ADDR_DEFAULT_OWNER` (0x004F9FDC) is set to the same value in the
  same breath. One of the three readings is wrong. Check both before writing
  that function.

- **`ReceivePacket`'s loop bound is UNSIGNED and that is not a detail.** The
  bogus-length test compares each part against the length the packet arrived
  with, not against what is LEFT, so a part longer than the remainder is
  accepted and drives the length field negative -- and read unsigned, a
  negative length is enormous, so the walk carries on off the end of the packet
  instead of stopping. A signed compare here quietly repairs that, which is not
  what this port is for. Written the first time with `int32_t` and corrected
  before it was committed.

- **A bad checksum does not stop the walk.** It logs, bumps that player's error
  count, and then parses a packet it has just been told is corrupt.

- **And the part length is re-read AFTER the handler has had the bytes**, so a
  handler that rewrote those two bytes would move the cursor somewhere else.
  Three original behaviours in one 256-byte function, none of which any A/B in
  this project can reach.

- **`0x0040FBB0` is a SECOND dispatcher**, for one message out of a packet --
  "Unknown Army Msg Item Type %d, msgtype:%d, item uid: %x; msgsize: %d" -- and
  it takes the slot where `CommDispatchMessage` takes the id. Two layers of
  dispatch, and the names had to distinguish them.

- **The self-naming sweep credited a string to the wrong function, and this is
  the first time that has been caught.** It listed `0x00411BD0` as carrying
  "TIMING OUT PLAYER %d %s". That string belongs to `0x00411C20`; the two share
  one `functions.tsv` entry, and the sweep attributes by ENTRY. So the sweep's
  output is "a name somewhere in this entry", not "this function's name" --
  read the body before believing the pairing, exactly as with `merges.py` and
  the COM ranking.

  `0x00411BD0` carries no string at all. From its body it is the host telling
  us how to send: the value becomes the comm object's SEND FLAGS -- the third
  argument `ArmyMessageFlush` hands `SendGameMsg` for every outgoing packet --
  and two more fields go into our own flow record.

- **It is the one message in the family that ignores its dpid**, looking up the
  flow record by OUR id instead. And if we have no flow record yet the two
  fields are dropped while the send flags are kept anyway.

- **This commit is a clean demonstration of the two coverage figures.** The
  honest one moved 80 bytes; the entry-crediting one moved 704, because
  `0x00411BD0` and `0x00411C20` share an entry and patching one credits both.
  That is the whole argument for `tools/reconstructed.py` in a single step.

- **The pause mask is one bit per player per REASON, and `RemoteGamePause` is
  where a peer's bit moves.** Two independent blocks, not a switch: bit 0x0008
  of the message's flags drives the `0x10 << slot` family and bit 0x10000 the
  `0x20000 << slot` family, and a message carrying both runs both. That is what
  the 767,153 `GetPauseFlags` reads in a Boot Camp run are testing against.

- **A slot above 3 is not clamped, it is not handled.** Each block is four
  explicit compares rather than a shift, so a fifth player would fall out with
  no call made and the mask left at zero -- which then suppresses the log,
  since it is guarded on the mask as well as on verbosity. Reproduced.

- **`GetPauseFlags()` takes no arguments and the call site pushes two.** They
  are the last two varargs of the log line that follows, pushed first because
  cdecl evaluates right to left; the call simply steps over them. Reading that
  as a two-argument function -- which is what it looks like -- would have
  invented a signature. `frame.h` already had it right.

- **`SendMapMsg` was wrong in two ways and its own caller is what said so.**
  Reconstructing `ReceiveStartGameMsg` a commit later showed the call site
  pushing TWO arguments and cleaning eight, where the body reads one -- and all
  three call sites do. So it has two parameters and reads only the first.
  Behaviourally identical under cdecl, but a signature that is wrong in the
  header is the thing CLAUDE.md already warns about under private typedefs.

  Worse, my comment said it tells the other players "which map is chosen". It
  does not: the value is a RESULT CODE. This function sends 7, 0x00411830 sends
  5, and `ReceivedMapMsg` switches on 0..8 calling 4 nominal. I named the
  argument from the function's name instead of from a caller. Both corrected.

- **The seed arrives at 0x0190 of the start record**, straight into the global
  `SendGameStartMsg` chose it in -- so both halves of that story are now
  reconstructed. And "Seed is %d" prints a literal 0 in the RECEIVE half too,
  so the seed is never in the log at either end.

- **One failing player stops the game for everyone.** The failure flag is
  checked once, after the whole loop, so a single `DPLAY ERROR SENDING TO` or
  `FlowQ creation Failure` reaches "Error in start" and nothing starts. A zero
  player count skips both the loop and the check.

- **0x0469 is a seventh window message and nothing handles it.**
  `ReceiveStartGameMsg` is its only sender in the image and `WndProc` has no
  case for it, so `DefWindowProc` eats it. The six in `winproc.cpp`'s table
  were found by decoding forward from each `push` to its `PostMessageA`; this
  one was missed, so that sweep was not complete.

- **The receive side has a dispatcher and it is ours now.** `0x0040FEA0`, an
  eighteen-arm jump table on the message's first dword, reached from
  `0x004026D5` and gated on `0x0404` of the comm object -- which is NOT the
  `0x0400` that `COMM_OFF_STARTED` and `COMM_OFF_LOCAL` both already name.

- **Type 2 returns in silence; types 4, 12, 13 and 16 are LOGGED as unknown.**
  So the original distinguishes a message it knows and ignores from one it does
  not know, and a reconstruction that folded them together would lose a log
  line that only a live session could show.

- **Three different host tests sit side by side in one function.** Types 9, 17
  and 18 return unless this machine is the host -- on top of the identical test
  inside the handler each would have called. Type 8 tests the host only to LOG
  that it should not have received the message, and calls the handler either
  way. Everything else does not test at all.

- **Type 10 pushes two arguments at a handler that takes none.** cdecl, so the
  arguments are simply dropped; `ReceiveEndSetupMsg` really is `void(void)` and
  the call site really does push the message and the sender.

- **`MsgSlotB0`'s first argument is a PLAYER record, not the comm object.** The
  type 11 arm passes what `FindPlayerById` returned. `msgslot.h` describes that
  family as fields of the comm object, which is where the six writers were
  first read; one call site says otherwise and both may be true if the player
  records live inside it, but the header should not be trusted on that point
  until it is checked.

- **`COMM_OFF_STARTED` and `COMM_OFF_LOCAL` are two names for `0x400`**, and
  `checkglobals` cannot see it -- that ratchet tracks `ADDR_` macros, not
  `COMM_OFF_` ones. There is a second family of names with no ratchet on it.

- **The selftest link drew a module boundary, and it drew the right one.**
  `msgslot.cpp` is in `SELFTEST_SRC` because its slot writers, its latency ring
  and `CommRemoveKeyed` are pure functions with recorded vectors. The comm
  MESSAGE family had been growing in the same file, and it can never have a
  vector -- every one of those functions reads the comm object, logs, repaints
  a dialog or plays a sound. The moment `ReceivedMapMsg` needed `PlaySoundAt`,
  which lives in `win32/audio.cpp` where the selftest deliberately does not
  reach, `selftest.exe` stopped linking.

  This is the second time `selftest-link` has caught a real structural
  question, and the answer was different from last time: `SendGameStartMsg`
  moved to the module of the functions it drives, while here the module itself
  had to split. `src/game/commmsg.cpp` is the message half; `msgslot.cpp` keeps
  what can be checked offline. The header stays whole, because it documents a
  field and both functions that touch it, and that is not what the linker was
  asking about.

- **The three lobby settings and their receive halves are done.** `SendMapMsg`
  returns 1 WITHOUT sending when this machine is the host -- the host has
  nobody to tell -- and its log is mislabelled: it prints the ARGUMENT under
  "Error = %d" while the send result it just took is never printed at all. The
  same author's "Seed is %d" in `SendGameStartMsg` pushes a literal 0.

- **`ReceivedMapMsg`'s arms do not line up with its own log text.** The message
  says "Result = %d (4 is nominal)", and 4 takes the same arm as the failures:
  clear the slot's flag and play sound 3. Only 0 sets it. 5 and 7 do nothing,
  as does anything above 8. Taken from the table at 0x00411998, which as always
  in this image is not the order the arms are laid out in.

- **The colour and team receivers differ in the middle and nowhere else.** The
  colour one goes through `CommSetArmyColour`, which SWAPS the colour with
  whoever already had it and can refuse with -1; the team one writes the field
  with no check of any kind. Both then repaint the current dialog and send the
  player list.

- **`make -s check` was committed against while FAILING, and the reason is
  worth knowing.** Its last line reads "all static checks pass" only on
  success, but `tail -1` of a failing run shows whatever the last tool printed
  -- here "generated ok" -- which reads exactly like a pass. The exit code was
  1. `checkclaims` had caught the Lock/Unlock bracket count going from 10 to 11
  the moment `TyperPaint` was written, which is precisely what that tool is
  for. Read the exit status, not the last line.

- **`ab.sh quit` compared the title screen and nothing else.** The only frame
  it took was before the QUIT click, so CONFIRM GAME EXIT -- the dialog the
  configuration exists to reach, and the one that runs the typewriter label and
  the QUIT GAME destructor -- was never in the pixels at all. It takes a `dlg`
  frame now.

  The wait before it is the part that needed measuring. That dialog's body
  reveals one character every 100 ms, so a shot taken too early catches the two
  sides at different characters and is unsynchronised by construction. "Are you
  sure you want to quit?" settles in about three seconds; shots at 4 s and 14 s
  are identical, so six is the margin.

- **The default pixel budget of 500 is too loose for a MENU configuration, and
  that has now been found twice in one session.** Dropping the trailing line
  from `TyperPaint` deletes the whole message and moves **361** pixels; the
  default passed it, exactly as it passed the 336 of the scroll-bar mutation.
  A line of menu text is about 360 pixels, so a budget that cannot see 361
  cannot see a missing line. `quit` is 200 now, with `controls`, `difficulty`
  and `audiovol`.

  The gameplay configurations are a different case and 500 is right there: the
  scene moves between runs and that is the noise floor. The number was never
  wrong, it was being applied to screens it was not measured on.

- **Both typewriter counters are blind**, so the frame is the only evidence.
  `blindspots.py` lists `TyperPaint` and `TyperUpdate` as unable to move --
  every caller is ours, through the vtable. That is why the shot mattered more
  than usual here.

- **The percentage in the table above was an over-count, and had been for the
  whole project.** It asked "does a patched address fall inside this
  `functions.tsv` entry", which credits the WHOLE entry to whichever function
  in it was patched -- the same defect `tools/coverage.py` was fixed for, where
  reconstructing `AudioTimerProc` marked `OpenAudioStream`'s COM calls covered
  a commit before they were.

  It bites hardest exactly where the work is easiest. The fifteen dialog
  destructors are two instructions each, about 400 bytes all told, and they
  moved the naive figure 3.8 points -- because each one sits inside an entry
  holding a whole dialog's implementation. A jump like that is the tell.

  `tools/reconstructed.py` now prints both, splitting merged entries at their
  referenced starts the way `merges.py` already does for the boundary count.
  The honest figure is **25.4%**, the old one 32.1%. It is a tool rather than a
  line of shell because it had been recomputed by an ad-hoc script every
  session, which is how it drifted in the first place.

- **The dialog hierarchy is three deep and the middle level had no name.**
  0x0046FC84 sits under the ICON, whose destructor it jumps to, and fifteen
  full-screen dialogs sit under it -- SELECT MAP, DIFFICULTY, QUIT GAME,
  REPLAY, AUDIO, OPTIONS, DELETE GAME, the overwrite confirm, DELETE PLAYER,
  CONTROLS, SELECT PLAYER, the recruit name box, LOAD GAME, the plain message
  box and the in-game ESCAPE menu. Each is named from the bitmap its
  constructor loads, which is the only thing that tells them apart.

- **Fifteen identical destructors are still fifteen functions in the image.**
  Unlike CommEndSetup, there is nothing here for the linker to fold: each
  stamps a DIFFERENT vtable constant. So the macro in widget.cpp is a way of
  writing them, not a claim that the original had one function -- and the
  reason for it is that fifteen chances to mistype a vtable address are not
  fifteen pieces of evidence.

- **`ab.sh quit` is the check for a destructor, and it is a real one.**
  `ReportLeaks` prints "Unreleased memory (%d) blocks:" on DLL_PROCESS_DETACH
  and that line is inside the compared log, reading (0) on both sides. A
  destructor that forgot a free would move a number that is in the diff.

- **The dialog dispatcher's jump table is another case of layout order lying.**
  0x00426400 has 21 arms selected by 0x00511DBC, and reading the call sites
  top to bottom numbers them wrong: arm 7 is the tenth call laid out. Take the
  order from the table at 0x00426518. AUDIO is id 19.

- **Next in this layer, both read and not yet written.** The class at
  0x0046FD24 is a TYPEWRITER message label: its constructor word-wraps the
  text into a `|`-separated buffer at 0x0058, its update reveals one more
  character every 100 ms and plays a click, and its painter draws the revealed
  prefix line by line. Six confirm dialogs build it, including CONFIRM GAME
  EXIT -- so `ab.sh quit` reaches it. And 0x0046FC5C is a four-sprite checkbox
  with four ink bytes chosen on (focused, checked) and a caption drawn after
  the sprite.

- **The AUDIO CONTROLS dialog is the only screen with a scroll bar**, and
  `tools/ab.sh audiovol` is new for it: OPTIONS -> AUDIO reaches three of them
  -- the SOUND EFFECTS, MUSIC and VOICE sliders -- each with an ltarrow and an
  rtarrow child. `ctl widgets` says class 0x0046FCFC appears nowhere else.

- **And the widget oracle CANNOT see what that configuration is for.** A scroll
  bar's own sprite lives at 0x0064, not at the base's 0x0038, so every bar
  prints `spr=-1` and a wrong bar would dump identically. The pixels are the
  evidence here, which is the reverse of `controls`, where the pixels are blunt
  and the tree is sharp. Ask which of the two can see a given field before
  claiming a screen is covered.

- **The first version of `audiovol` could not fail, and the mutation is what
  said so.** Dropping the thumb offset from `ScrollBarPaint` moved 336 pixels
  against the DEFAULT budget of 500, and the run reported A/B clean. Worse, the
  four arrow clicks it makes to move a thumb were photographed by nothing: the
  `dlg` frame is taken before them and the final frame after CANCEL has closed
  the dialog. Both fixed -- a `mid` frame after the clicks, and a budget of 200
  measured in both directions (clean 45/45/50, mutation 336).

- **The scroll bar's two axes are not symmetric.** x is centred on the widget's
  width less a span field and then shifted by an offset field, which is what
  moves the thumb; y is centred on the SPRITE's height with nothing added. Only
  one axis can scroll, which for a horizontal bar is the point. And both halves
  halve AFTER the subtraction where `WidgetPaint` halves each side BEFORE it --
  a different rounding on odd values, kept as each has it.

- **The arrow children have no constructor.** The scroll bar builds each one by
  calling the BUTTON constructor and stamping vtable 0x0046FCD4 over it, and
  the arrow's destructor is a single `jmp` to the button's -- so it stamps
  VTABLE_BUTTON and never its own.

- **Fifteen dialog destructors are the same two instructions**: stamp my own
  vtable, jump to the dialog base at 0x00454B90. The base is one level under
  the ICON, whose destructor it jumps to in turn. That is the next batch, and
  `ab.sh quit` is its check -- `ReportLeaks` prints "Unreleased memory (0)
  blocks:" into the compared log, so a destructor that forgets a free moves a
  number that is in the diff.

- **Three identical copies of a block is an INLINE FUNCTION, not three
  transcriptions.** The end-of-setup scan appears at the tail of
  `SendGameReadyMsg`, at the tail of `ReceiveGameReadyMsg`, and once on its own
  at `0x00410CE0` with a caller of its own -- instruction for instruction the
  same, modulo register allocation. That is what MSVC does with an inline
  member function it declines at one site out of three. Written ONCE here and
  called from all three, which is both less to be wrong about and closer to
  what the original source said.

- **And the standalone copy was already named.** `ADDR_COMM_END_SETUP` had been
  in `orig.h` since the comm survey; I added `ADDR_SEND_END_SETUP_IF_READY`
  beside it without grepping the address first -- the exact mistake CLAUDE.md
  warns about, within one function of writing a comment about it.
  `checkseams` is what caught it, not `checkpatches`: the alias ratchet counts
  the surplus, and one more alias sat under its baseline. **A ratchet with a
  baseline cannot fail on the first offence.**

- **The handshake pair is not symmetric, and that is the original's design.**
  The ready-to-load pair splits host and client with an early return each. The
  READY pair does not: `SendGameReadyMsg` runs the host scan for whoever calls
  it, because a host marking ITSELF ready may be the last one the scan was
  waiting on.

- **All six handshake functions are done.** The next thing on that path is
  `0x00418F90` -- 24 bytes, a widget activate handler registered by
  `push 0x418f90` at `0x004192C1`: play sound 0, then `SendGameReadyMsg(1)`.
  That is the READY button itself.

- **Closing a seam can move a function to another FILE, and this one did.**
  `SendGameStartMsg` was written in `msgslot.cpp`, and its three comm callees
  turned out to be reconstructed -- so `checkseams` demanded direct calls, and
  direct calls turned reaches-by-address into LINK dependencies on
  `win32/dplay.cpp`. `msgslot.cpp` is in the selftest link, which deliberately
  does not pull in DirectX, so `selftest.exe` stopped linking.

  The fix was not a stub or an exclusion: the function belongs in `dplay.cpp`,
  beside the three methods it drives. `selftest-link` is the check that caught
  it, and its own message predicted the cause exactly.

- **The host picks the shared RANDOM SEED here**, reads it from the clock, keeps
  it in `0x00512314` and sends a copy -- so every machine's RNG starts from the
  same number. That is the only place found that sets it.

- **Two of the original's oddities in one function.** The opening log is NOT
  gated on the comm verbosity field, unlike every other function in this group.
  And the second log says "Seed is %d" while a literal 0 is pushed for it, so
  the seed it reports is always zero and the value actually used is never
  printed. Both kept.

- **Five of the six handshake functions are done.** Only `SendGameReadyMsg`
  (352 B) remains.

- **The original disagrees with itself about a null check.**
  `SendGameReadyToLoadMsg` and `ReceiveGameReadyToLoadMsg` end with the SAME
  two-call lobby repaint, and the receive half tests the dialog pointer while
  the send half does not. One of the two is wrong. Both are reproduced as
  written -- a crash on a null dialog is the original's behaviour, and adding
  the guard to the send half would hide a real difference between the two
  paths.

  Worth noting for the port: if the native build ever wants that guard, it is a
  deliberate divergence and should be marked as one, not slipped in.

- **Four of the six handshake functions are done.** `SendGameReadyMsg` (352 B)
  and `SendGameStartMsg` (256 B) remain.

- **The member-name sweep found exactly three**, and they were worth having:
  `m_ArmyReady` (`0x0274`), `m_ArmyReadyToLoad` (`0x0270`) and `m_pLobby`.
  Two of them are adjacent fields of the 112-byte per-army record, and having
  the name before reading the function made `ReceiveGameReadyMsg` legible on
  the first pass rather than the second.

  Three is a small return, but they are the game's OWN identifiers. Worth
  re-running whenever the log corpus grows.

- **"Occupied" means `player id != -1` only.** `ReceiveGameReadyMsg` decides
  setup is over when every occupied slot is ready, and skips only `-1` --
  while `AM2_PLAYER_ID`'s own note says "0 or -1 is none". So a slot holding 0
  must be ready for the game to start. Left as the original has it, and
  flagged rather than smoothed.

- **Three of the six handshake functions are done.** `SendGameReadyMsg`
  (352 B), `SendGameReadyToLoadMsg` (256 B) and `SendGameStartMsg` (256 B)
  remain -- all send-side, all read-verified only.

- **A log string handed over an original MEMBER NAME.** "Setting
  m_ArmyReadyToLoad[%d] to %s" places that array at `0x0270` of the 112-byte
  per-army record -- the same stride `0x0040F5A0` indexes -- and it is the
  original's own identifier, `m_` prefix and all, not a name of ours. Worth
  sweeping the log corpus for others: a format string that prints a field
  usually names it.

- **The comm side drives the MENU, which nothing had shown before.**
  `ReceiveGameReadyToLoadMsg` repaints the lobby through the current dialog's
  slot 2 then slot 1 -- the same update-then-paint pair the widget layer uses
  everywhere. So the handshake and the widget work meet here, and the widget
  slots being reconstructed is what made this function legible at a glance.

- **`0x0065A058` had two names and one is mine.** `control.c`'s `WD_ROOT` was a
  duplicate of `ADDR_PAINT_OBJECT`, introduced when the widget dump was
  written. Fixed by reuse. The `ctl widgets` root and WndProc's repaint target
  are the same object, which is worth knowing: the thing WndProc repaints IS
  the current dialog.

- **`0x3D8` still has two names**, `COMM_OFF_IS_HOST` and `COMM_OFF_READY`.
  The first is evidenced -- "from DPCAPS_ISHOST" -- and this function's
  host-only gate agrees with it. Not collapsed yet.

- **The six private window messages live in `orig.h` now.** They were defined
  in `winproc.cpp`, which HANDLES them, and the comm side POSTS them -- so the
  first comm function to need one would have duplicated the constant. One
  constant in two files is one too many, and this session has spent several
  commits undoing exactly that for `g_` macros and `ADDR_` names.

- **`ReceiveEndSetupMsg` is done; five handshake functions remain.**
  `SendGameReadyMsg` (352 B), `ReceiveGameReadyMsg` (304 B),
  `SendGameReadyToLoadMsg` (256 B), `ReceiveGameReadyToLoadMsg` (224 B),
  `SendGameStartMsg` (256 B). All self-naming, all in the same band, and none
  exercisable without a second player -- so the whole group is read-verified
  and the A/B can only confirm it does not break single player.

- **The self-naming sweep missed SEVEN more, because it required a colon.**
  `RemoveInventoryItem` logs exactly `"RemoveInventoryItem\n"` -- no colon, no
  arguments -- and the regex wanted `Name:`. Widening it to "the whole message
  is an identifier" finds six comm handshake functions as well:
  `SendGameReadyMsg` (`0x00410A10`), `ReceiveEndSetupMsg` (`0x00410B70`),
  `ReceiveGameReadyMsg` (`0x00410BB0`), `SendGameReadyToLoadMsg`
  (`0x00410D90`), `ReceiveGameReadyToLoadMsg` (`0x00410E90`),
  `SendGameStartMsg` (`0x00411000`).

  So the list is 14 + 7 = 21, and the six comm ones are the handshake CLAUDE.md
  says is verified by reading because it needs a second player.

- **A unit's inventory is six weapon uids at `0x054C`**, with the one in hand
  at `0x0568`. `RemoveInventoryItem` shifts, clears the sixth, and fixes the
  selection -- and the three selection cases are not symmetric: equal resets to
  0 and re-selects, above slides down, below is untouched. Its counter reads 0
  on Boot Camp; nothing there loses a weapon.

- **`UseInventoryItem` (`0x00449760`, 256 B) is READ; four callees need names
  first.** The body is plain enough:

  - In a multiplayer session (`ADDR_MP_SESSION`) it returns immediately unless
    `0x0040F560` approves the unit's army; in single player that test is
    skipped entirely.
  - `comm[0x418]` gates two log lines, so it is a verbosity flag.
  - The item is `unit[0x54C + slot*4]` -- an inventory of uids -- looked up
    through `0x0045EE80`, with `0x0045EE20` (`KindInSetA`, already ours)
    vetting its kind.
  - `item[0xCC]` is a USE COUNT. It is decremented, and only when it reaches
    zero does the item leave: `0x00447990(unit, slot)` clears the slot and
    `0x0044C150(unit, item, slot, 0, unit[0x12])` is the drop, whose log line
    is "droping item:%x".
  - `item[8] |= 2` happens on BOTH exits of that last branch, taken or not.

  **One of the four is now named, and it changes what the function means.**
  `0x0045EE80` looks a uid up through `ADDR_OBJ_BY_UID` and requires kind 4,
  complaining **"uid wasn't a weapon!"** when it is not. So it is
  `WeaponByUid`, kind 4 is a WEAPON, and the "inventory" at `unit[0x54C]` is a
  weapon inventory -- which makes `item[0xCC]` an AMMO count rather than a
  generic use count, and the drop at zero the gun being thrown away when it is
  empty. Named from its own message, not from this call site.

  Three still need reading: `0x0040F560`, `0x00447990` and `0x0044C150`.
  `0x0040F560` and `0x0040F5A0` are a thiscall pair on the comm object -- the
  second indexes **112-byte per-army records** at `comm + army*112` and tests
  fields at `0x020C`, `0x0214` and `0x025C` against `comm[0x3D8]`. That record
  size is worth having on its own; the pair's meaning is not established.

- **`LoadDibFlipped` cannot run in this installation, and that is measurable
  rather than inferred.** Its only caller globs
  `%02d_%03d_%02d_*.msk` out of a `masks` directory, and the GOG build ships
  **no `masks` directory and zero `.msk` files anywhere in the prefix**. So the
  counter's 0 is neither a blind spot -- `blindspots.py` agrees the caller is
  original -- nor a path not driven. The content simply is not there.

  Worth doing anyway: it is 192 bytes, it is on the self-naming list, and the
  reading corrected a swap before it shipped. But it is verified by reading and
  no amount of driving will change that.

- **Two header fields nearly went in swapped.** `hdr[0x14]` is the LISTED SIZE
  -- checked for zero, passed to `malloc`, and returned through the out
  parameter -- while `hdr[0x08]` is the block count only `ReverseBlocks` sees.
  The first draft had them the other way round, which would have allocated the
  block count and reported it as the size. The compiler caught it only because
  an unrelated type error forced a re-read of the same lines.

- **`CreateTimer` is in, and it is what settled the clock.** 1,000 records of
  `{start, period, count, id}` at `0x0050C370`, a slot free when its id is
  zero, and a live count at `0x0050C36C`. Two refusals with DIFFERENT
  thresholds: a low-priority request is dropped past 900 live timers, a request
  with a period over fifteen seconds past 950. So a slow timer outranks a
  low-priority one and both outrank nothing -- an ordinary request is refused
  only when the table is genuinely full.

  A schedule that has already begun is CAUGHT UP rather than fired late: the
  elapsed repeats are counted off `count` and added to `start`.

- **Its one caller passes a period of ZERO**, and that is safe only by
  ordering. With `count == 1` the already-elapsed test answers before the
  catch-up divides by the period. Two fires at period zero would divide by
  zero and nothing in the function stops it. Reproduced with the reasoning
  beside it, because a reader tidying that branch could easily "fix" it into a
  crash.

- **`MsgListRemHead` is in and reads 0**, which is exactly what the earlier
  mutation predicted: single player FILLS the message-buffer pool at startup
  and never draws from it. `MsgListAdd`'s 400 appends were that fill, and
  breaking the list's forward link was invisible for the same reason. The pair
  is now reconstructed and both halves are verified by reading.

- **"Empty List!" is gated on one specific list**, `ADDR_MSG_LIST_POOL`,
  because an empty POOL means the game has run out of message buffers while an
  empty ordinary queue is simply idle. That is the kind of thing a generic
  linked-list reconstruction would smooth away.

- **14 self-naming functions are still original, not 21** -- six of the
  entries were never names at all. A message beginning `ERROR:`, `Error:`,
  `Warning:` or `List:` says nothing about which function printed it, and one
  is actively misleading: `0x004372A0` prints "ERROR: SetObjScriptState was
  called with %s", which names a DIFFERENT function. Taking a name from that
  table without reading the body is how `0x00423200` nearly became "ERROR"
  instead of a DIB loader.

  The real fourteen, and the item and unit ones run during a mission so they
  can be A/B'd:

  `RemMsg` (`0x00401410`), `Resend` (`0x004014C0`), `DestroyFlow`
  (`0x004029B0`), `ArmyMessageFlush` (`0x00410420`), `DefGameParse`
  (`0x00424590`), `itemDeployMessageSend` (`0x0042AA50`),
  `itemDeployMessageReceive` (`0x0042AF30`), `DamageTrooper` (`0x00447A40`),
  `DeployTrooper` (`0x00449250`), `UseInventoryItem` (`0x00449760`),
  `UpdateTrooperAction` (`0x0044AFB0`), `troopMessageReceive` (`0x0044C590`),
  `ExitAllFromVehicle` (`0x0045AE30`), `CreateWeapon` (`0x0045F0C0`).

- **Two ownership conventions sit side by side in one hierarchy.** The list
  box's destructor tests BOTH an ownership flag at `0x0064` and the pointer
  itself before freeing its row array; the icon's and the blinker's release
  their sprites with no null test at all. Neither is wrong -- `ReleaseSprite`
  copes with a null and `free` would too -- but a reader who generalises from
  one to the other will write a guard the original does not have, or drop one
  it does.

- **The vtable survey says the widget layer is done for every menu screen that
  can be driven.** Five screens checked -- CONTROLS, the multiplayer battle
  dialog, DIFFICULTY, the film archive and SINGLE PLAYER -- and what is left on
  any of them is per-dialog DESTRUCTORS in the menu band (`0x0044E4F0`,
  `0x004510D0`, `0x004518E0`, `0x0042FF40`, `0x00455B80`) plus two list-box
  updates. The shared bases cover everything else those screens instantiate.

  So the widget layer proper is finished, and what remains under these dialogs
  is dialog logic rather than widget logic.

- **`BlinkerStart` runs, and finding out how confirmed the unwritten function's
  reading.** It had read 0 on every run and was verified by reading only.
  Driving the DIFFICULTY dialog and moving the pointer across the list rows
  takes it 0 -> 2 -> **10**: two from clicking a row, eight more from moving
  over them. So the analysis of `0x00455340` below is right about the part
  that matters -- a change of hovered row starts a blink -- established by its
  effect rather than by more reading.

  **It needs `mouse move`, not `cursor`.** `cursor` writes the position
  globals and nothing else, so `g_mouseMoved` stays clear and every
  hover-gated path is skipped; the first attempt poked the cursor across all
  three rows and moved the counter not at all. Anything gated on movement has
  to go through the relative path.

- **`0x00455340`, the list box's update, is READ but not written -- 2 KB and
  branchy, and the reading is the hard part.** What it establishes:

  - It opens with an optional per-frame callback at `0x006C`, called with the
    widget, before anything else.
  - Hover to focus like the edit box, then it computes the row under the
    POINTER from the cursor's y: `(cursorY - rect.top - 4) / 14 + topRow`,
    clamped to `count - 1`. The division is the compiler's magic-number
    sequence -- `imul 0x92492493; sar edx, 3` with the sign add-back -- which
    is division by **14**, independently confirming the row height that
    `ListTakeFocus`'s arithmetic gave.
  - When the row under the pointer CHANGES and `0x0094` holds a widget, it
    calls `BlinkerStart(that, 0x46, 1)` -- so moving over a list flashes the
    associated indicator once for 70 ms. That is what the blinker is FOR, and
    it is the first thing found that starts one.
  - The rest, about 1.5 KB, is the mouse-button and keyboard handling.

  Two independent routes to the 14-pixel row height, and a use for the blinker,
  are worth having even before the function is written.

- **`checkglobals` was keyed on the ADDR_ NAME, which made it blind to the
  case it exists for.** Two `g_` names sitting on two `ADDR_` aliases of one
  byte looked like two unrelated globals. It surfaced only because collapsing
  those `ADDR_` aliases made the surplus go UP: `ADDR_SEQ_BAR_BG` and
  `ADDR_PIXEL_FORMAT_BYTE` became one name, and three `g_` names suddenly
  landed on one key.

  Keyed on the ADDRESS it reports **39** where it reported 28. A number that
  rises after a tooling fix is the tool getting more honest, and this is the
  second time this project has seen that -- `merges.py` did the same thing to
  the boundary count.

- **`0x00502AD9` is the palette index this game fills with**, and it had two
  names and three uses: the sequence bar's unfilled colour, the fill
  `AttachPalette` is handed, and now the list box's background. All three were
  local descriptions of one thing. `ADDR_BACKGROUND_COLOUR` replaces both, and
  the three `g_` names with them -- `ADDR_` aliases 31 -> 30, `g_` surplus
  39 -> 37. "Pixel format byte" was the actively misleading one; it is not a
  pixel format.

- **The blinker derives from a one-sprite ICON**, established by its
  destructor chaining to that class's rather than to the base. So the
  multiplayer dialog's "send" dot is an icon that can flash: one sprite from
  the parent at `0x0058`, two more of its own at `0x0060` and `0x0064`, and the
  blink swaps between the latter pair.

  Both destructors release their sprites with NO null test. Worth knowing
  before adding a guard the original does not have -- `ReleaseSprite` is
  trusted to cope, and it does.

- **`tools/ab.sh difficulty` is a third menu configuration**, OPTIONS ->
  DIFFICULTY, six nodes, identical trees and 0 pixels. It exists because that
  dialog is the ONLY place the list box at `0x0046FCC0` is instantiated, so
  its painter (`0x00455180`) and its update (`0x00455340`) are checkable there
  or nowhere. Found by dumping the tree and crossing the vtables against the
  patch list, which is now the standard way to pick a target.

- **`ListDraw` is done, and it is one of the few things in this layer with a
  real defect signal.** Dropping the highlight fill on the selected row -- the
  green bar in SELECT DIFFICULTY -- is **3,299 pixels**, well over budget. Most
  of this layer sits between 0 and 249; a filled row is big enough to see.

- **The list's row records are 260 bytes.** `0x00455180` computes the offset as
  `((idx << 6) + idx) << 2`, which is `idx * 65 * 4`, into an array at `0x0060`
  whose first dword is the row count. It draws rows `0x0074` through
  `0x0074 + 0x0078` and picks an ink from four different fields -- `0x0080`
  normally, `0x0084` and `0x0088` for the selected row depending on whether the
  left mouse button is down, and `0x008C` for another case still. Not yet
  reconstructed; the colour selection needs the rest of the body read.

- **What is left on the two drivable dialogs is now two functions.**
  `0x004510D0` on CONTROLS (2,063 B) and `0x0042FF40` on the multiplayer one.
  Everything else either screen instantiates is reconstructed.

- **The MSVC SEH prologue on a destructor is not reproduced, deliberately.**
  Nothing in this program throws -- VC6's `operator new` answers NULL and the
  game tests it at `0x00451251` -- so the registered frame is never consulted.
  The cost if that were ever wrong is a skipped base destructor during an
  unwind: a leak, not a crash. CLAUDE.md carries the reasoning and the failure
  mode. That unblocks six destructors across the two drivable dialogs.

- **A leaked sprite has no signature in any test here.** `ButtonDestruct`
  releases its three sprites when `0x0075` says it owns them, and suppressing
  that entirely leaves `controls`, `multi` AND `quit` clean -- the last was
  worth trying, because its log carries an "Unreleased memory (N) blocks" line,
  but sprites do not reach that counter. So the release is verified by reading.

  `ButtonDelete` reads 13 on a run that opens and cancels the CONTROLS dialog,
  so the path itself is thoroughly exercised; it is only the RELEASE that
  nothing can see.

- **The two-state indicator is a BLINKER, and it is the toggle's other half.**
  `0x00456D40` flips `0x006C` on a timer, counts flashes down and stops -- and
  `0x006C` is the same field `TogglePaint` reads to choose its sprite, so the
  blink IS the sprite swap. A blink always ends in the OFF sprite whatever
  count it was given, because reaching zero clears the state as well as the
  active flag. On the multiplayer dialog these are the "send" dots.

  Suppressing the flip is **106 pixels** -- under `multi`'s budget of 500, so
  the pixels pass it -- and the WIDGET TREE catches it. Second time the oracle
  has caught something the budget could not.

- **Both drivable dialogs are now nearly all ours.** Crossing the classes each
  screen instantiates against the patch list leaves, on CONTROLS: two SEH
  destructors. On the multiplayer dialog: three SEH destructors and
  `0x0042FF40`. Everything else on both screens is reconstructed.

  So the next real work is elsewhere -- either the SEH destructors, which need
  a decision about reproducing MSVC exception frames, or screens neither
  configuration reaches.

- **`ctl widgets` prints the vtable ADDRESS now, and it turned target selection
  into a lookup.** The CONTROLS dialog uses exactly three of the thirty-three
  classes -- `0x0046FB80` x21, `0x0046FB94` x1, `0x0046FC34` x3 -- so crossing
  those against the patch list said, in one command, that only three functions
  on that whole screen were still original. Two are SEH destructors; the third
  was `0x00450D50`, and it was the key-capture row.

  **Ask which classes are on the screen before choosing what to reconstruct.**
  Ranking by size picks functions that may never run -- `MultiSpritePaint` is
  9,081 calls that never draw. Ranking by what the drivable screens actually
  instantiate picks functions that can be checked.

- **The key rebinding is the best-verified thing in this session.** Click
  CUSTOMIZE CONTROLS, click FORWARD, `key 0x24 tap`: the row reads **`j`**.
  Click BACKWARD and press it again: BACKWARD reads `j` and FORWARD reads
  **`None`**. That is the 95-entry key table, the index, the name pointer, the
  repaint AND the twenty-one-row duplicate-clearing loop, all confirmed by
  looking at the screen. No budget, no second run.

  The 21 rows are the same 21 the widget tree counts on that dialog, which is
  two independent routes to the same number.

- **`MultiSpritePaint` runs 9,081 times and never draws.** Its sprite is null
  on every call on the multiplayer path. Shifting the drawn position five
  pixels changes nothing; returning outright before the blit changes nothing.
  So the placement and the array index are covered and the centring, the two
  intersects and the blit are verified by reading only.

  The probe that settled it is worth reusing: a mutation that DROPS a term
  proves nothing when the term is zero, but one that ADDS a constant
  distinguishes "the field is zero" from "this code never runs". The first
  attempt dropped the y bias, got 0 pixels, and would have been recorded as
  "the bias is zero here" -- which was the wrong conclusion about a function
  that was not drawing at all.

- **Where the second sprite of that class comes from is unknown.** The array at
  `0x0064` holds at most two, because `0x0064 + index * 4` reaches the index
  field itself at 2. Nothing driven so far populates either.

- **A first-seen pointer index cannot see a SUBSTITUTION, and that nearly cost
  the oracle its point.** The widget dump renumbers pointers so it survives the
  heap moving -- the same trick `tools/actdiff.py` uses. But forcing
  `TogglePaint` to the wrong sprite left the tree **identical**: the substituted
  sprite is simply first-seen at the same position and takes the same index.
  `spr=10` on both sides, 212 pixels apart on screen.

  Printing the sprite's own `id` alongside fixes it -- `sid=1576448` against
  `sid=1576449` on exactly the two "send" indicators. **Renumbering makes a
  dump reproducible and blind in the same stroke; carry one real datum beside
  every renumbered pointer.**

- **`ab.sh multi` captures the tree too**, 8 nodes, and it is what moved the
  toggle row of the table from "not caught" to "caught".

- **`drive.sh ctl widgets` is an EXACT oracle for the menu layer, and it is in
  `ab.sh controls`.** The CONTROLS dialog is 25 nodes and they come back byte
  for byte identical from the original and from the reconstruction, so the
  comparison is a `diff` and not a budget. Setting the base constructor's
  `0x0050` to 0 -- which every pixel frame reads as 0 -- changes all 25 lines.

  That converts most of the "not caught" column of the table above into
  something catchable. The remaining pixel-only defects are the ones that live
  outside the tree: `g_charHandler` (a global, 72 pixels) and the list row
  strip (a transient, 0 pixels).

- **Field `0x0040` had to come OUT of the dump.** It is the one the constructor
  never writes, so for any widget whose update has not run it is allocator
  junk -- 25, 1 and 27,346,604 across runs that were otherwise identical. Two
  hand-compared runs happened to agree, which is how it got into the first
  version and why `ab.sh` failed on its first real run. An uninitialised field
  cannot be part of an exact oracle.

- **The defect-signal table, kept current, because it is the honest measure of
  what a clean run here is worth:**

  | defect | pixels | caught? |
  |---|---:|---|
  | button never fires (`ButtonUpdate`) | 306,126 | yes |
  | width from height (`WidgetScreenRect`) | 305,939 | yes |
  | label cleared with ink (`LabelDraw`) | 17,110 | yes |
  | focus highlight never shown (`ButtonPaint`) | 249 | yes, by 43 |
  | toggle always ON (`TogglePaint`) | 212 | **yes**, by the widget tree |
  | handler never installed (`EditTakeFocus`) | **72** | **no** |
  | wrong caret glyph (`EditDraw`) | **34** | **no** -- and not in the tree either |
  | row strip not repainted (`ListTakeFocus`) | **0** | **no** -- unobservable |
  | two constructed flags, focus quirk, repaint deferral | 0 | no |

  `multi` has now measured 0 four times running, so a budget of 150 would catch
  the toggle. It has NOT been tightened: four samples is the same evidence that
  said `controls` was exact before a fifth run gave 45, and repeating that
  mistake the same day would be worse than missing a 212-pixel defect.

- **The list box's rows are 14 pixels tall, and the arithmetic says so.**
  `ListTakeFocus` computes `top + 14 * (0x58 - 0x74) + 4` and clips a strip 14
  tall, which is what establishes the row height, the top margin, and that
  `0x0058` is the row being singled out while `0x0074` is the first row on
  screen. Calling those two the selected row and the scroll origin is the
  obvious reading and nothing here evidences it further.

- **Its distinctive half is unobserved.** Skipping the row repaint entirely
  leaves `multi` at 0 pixels. It is an optimisation -- repaint one strip now
  rather than wait for the next full repaint -- and a settled frame cannot show
  the difference. `ListTakeFocus` runs once on that path, when the TCP/IP row
  is clicked, so what IS checked is the base take-focus underneath it.

- **`ButtonUpdate` is the widget layer's sharpest A/B, by a wide margin.**
  Suppressing the left handler on release -- so no menu button does anything --
  is **30,096 / 29,964 / 306,126** pixels on the three `controls` frames. Two
  orders of magnitude above the focus-highlight and caret defects in the same
  layer, because a button that does not fire means the next screen never
  appears at all.

  Ranked by defect signal, this layer now reads: button not firing 306,126;
  wrong widget geometry 305,939; label colour 17,110; button focus highlight
  249; typed text 72. The budgets only reach the top three.

- **The four classes that use `0x00454310` are the auto-repeat buttons**, and
  the left/right asymmetry in it is reproduced rather than reconciled: with
  repeat enabled the LEFT handler does not fire on first press, only arming the
  250 ms deadline, while the RIGHT handler does. Both then repeat every 150 ms.
  Nothing driven so far reaches the repeat path -- it needs a button held down
  past 250 ms -- so that half stays verified by reading.

- **`controls`' budget of 200 is only just tight enough.** Making `ButtonPaint`
  always show the unfocused sprite -- so no button ever lights -- moves **249,
  249 and 243** pixels on the three frames. It is caught, by 43 pixels. A
  button's focus highlight is simply not many pixels, and the same is true of
  most menu state.

  Together with the edit box's 72, that is the shape of this whole layer:
  correct-looking output and small defect signals. The budgets are near the
  limit of what a whole-frame comparison can do here, and the sharper checks
  are the ones that DRIVE -- press the key, look at where the game went.

- **The edit box types, and that is end-to-end evidence.** `EditTakeFocus` is
  what installs `g_charHandler`, so a field can only receive a character if our
  reconstruction ran. Driving `AM2_MULTIPLAYER=1` to ENTER BATTLE NAME gives
  `EditUpdate` 12,552 and `EditTakeFocus` 1, and `ctl "type Zulu"` puts
  **`Zulu_`** in the field -- text and caret both.

  The caret was predicted from reading the painter, which appends a literal
  `'_'` when `0x0044` is set, and then seen. Prediction first, observation
  second, which is worth more than either alone.

- **`tools/ab.sh multi` is in, and its pixel check is weak on purpose.** It
  drives MULTI-PLAYER -> TCP/IP -> SELECT -> START A WAR to ENTER BATTLE NAME
  under `AM2_MULTIPLAYER=1` and types into the field: 7 identical messages and
  **0 pixels**, three runs.

  It does NOT catch the defect it was built for. Making `EditTakeFocus` skip
  installing `g_charHandler`, so nothing typed ever appears, moves only **72**
  pixels -- "Zulu" in a menu font is small -- and no budget can sit below that
  and still survive a blinking caret. So the configuration covers the PATH,
  12,552 `EditUpdate` calls and a dialog nothing else reaches, and the handler
  itself stays checked by driving and looking.

  **Measure the defect signal, not just the noise floor.** Three clean runs
  said the noise was 0 and that was true and useless; what settles whether a
  budget is worth anything is how big a real error is.

- **`controls` does NOT reach the edit box.** Its key-capture boxes look like
  text fields and are a different class: all five `Edit*` counters read 0 on
  that configuration. The classes that use it are ENTER BATTLE NAME behind
  `AM2_MULTIPLAYER=1` and the campaign's RECRUIT dialog, which CLAUDE.md warns
  against driving. So the edit box is verified on the multiplayer path or not
  at all -- worth an `ab.sh` configuration of its own, since the whole path is
  drivable and CLAUDE.md already records the coordinates.

- **The `dlg` frame earned its keep, and it is the only one that did.** Making
  `FocusLabelDraw` always use the focused colour pair leaves the final frame at
  **0** and the mid frame at **0**, and puts the DIALOG frame **635 pixels**
  over its budget. So the extra shots are not redundant: each covers a screen
  the others do not, and this defect is invisible on two of the three.

  That also answers the note from earlier today about the mid frame "not doing
  what it was added for". The principle was right and the first test of it was
  simply the wrong mutation.

- **A whole subclass in six small functions.** The focus-highlighting label --
  vtable `0x0046FB80`, what the CONTROLS panel builds its captions from -- adds
  nothing to the plain label but a second colour pair, picked on `0x0044` and
  copied into the label's own ink and paper before delegating. That is why
  those two bytes are rewritten every frame instead of being set once by a
  constructor. `FocusLabelDraw` runs 132,192 times opening the dialog.

- **A silenced log looks exactly like a clean run, and that cost five
  configurations.** Patching `0x0045CAA0` -- which is `ADDR_LOG`, folded with an
  empty virtual because both are a single `c3` -- replaced the game's logger
  with a no-op. Boot Camp still loaded, the HUD still drew, the pixel figure
  stayed at its usual 22. Only the LOG changed, from thirteen game messages to
  zero, and the log is the half of the A/B that reports it.

  Two wrong diagnoses on the way, both worth remembering. "The recon side is
  crashing" -- it was not, it rendered a perfect mission. And "my manual run
  works, so it must be ab.sh" -- it did not; I was comparing an unfiltered
  703-line log against ab.sh's FILTERED count, and filtered, my run gave the
  same single line. **Compare like with like before concluding the harness is
  at fault.**

- **`tools/ab.sh all` is CLEAN, all eight configurations**, at `74701d8` --
  the first full-suite pass of this session and the first since the widget
  layer began.

  | config | log | pixels |
  |---|---|---|
  | `bootcamp` | 13 identical | 22 |
  | `windowed` | 5 identical | **0** |
  | `intro` | 4 identical | **0** |
  | `audio` | 13 identical | 22 |
  | `mission` | 13 identical | 172,775 -- live scroll, budget disabled |
  | `campaign` | 14 identical | 2,571 -- budget disabled |
  | `controls` | 5 identical | **0 / 0 / 0** on all three frames |
  | `quit` | 8 identical | **0** |

  `audio`, `mission`, `campaign`, `controls` and `quit` are the five that ran
  blind against a silenced log before the `ADDR_LOG` fix; all five match now.
  `mission` and `campaign` have their pixel checks disabled by design -- two
  unsynchronised runs of live play -- so their evidence is the log, and both
  logs agree exactly.

- **The subclass tails are laid out INDEPENDENTLY, and three classes now prove
  it at the same offsets.** `0x005C` is the font in a label, and in the class
  at `0x00454310` it is the auto-repeat enable. `0x0060` is the label's ink
  colour, the cancel handler in `WidgetUpdateCancel`'s class, and the
  auto-repeat DEADLINE in `0x00454310`'s. So `AM2_Widget` must stop at the base
  -- anything past `0x0054` belongs to whichever subclass is looking, and a
  single struct covering the tail would be wrong three ways at once.

- **`0x00454310` is the mouse update, and it is a button with auto-repeat.**
  Slot 2 for 4 classes, about 800 bytes, and fully mapped now:

  - place, then bail to the base update if there is no parent or `0x004C` is
    set;
  - `0x0040 = PointInRect(rect, g_cursorPoint)` -- the hover flag, and the one
    field `WidgetConstruct` deliberately never writes, because this computes
    it before anything reads it;
  - if the mouse moved, take focus through slot 3;
  - `0x0054` is the LEFT/activate handler and `0x0064` the right one. `0x0054`
    is the same field `WidgetUpdate` fires on SPACE and RETURN, which is a
    clean cross-check between the keyboard and mouse paths;
  - and when `0x005C` is set, holding a button repeats: `GetTickCount` through
    the IAT at `0x0046F084`, **250 ms** before the first repeat and **150 ms**
    between them, with the deadline in `0x0060`.

  Every arm ends the same way -- repaint self with its own rectangle through
  slot 1, then call the base update -- which is what makes 800 bytes out of a
  small amount of logic.

- **The edit box filters what can be typed.** `0x0068` holds a pointer to an
  allowed-character string and the constructor defaults it to `0x004853A8`,
  which is nearly all printable ASCII plus tab. The other one in the image,
  `0x00485360`, is ` a-zA-Z0-9!'&+-_` -- no path-hostile characters, so it is
  the player-name and battle-name filter. Relevant to CLAUDE.md's note about
  typing names: what a field accepts is data, not code.

- **The caret is an underscore, and `flag44` is what shows it.** The edit box
  painter copies its text into a stack buffer and, when `0x0044` is set,
  appends `'_'` and re-terminates before drawing; it also picks ink `0x0064`
  rather than `0x0065`. `0x0044` is set by `WidgetTakeFocus` and cleared by
  `WidgetRepaint`, so it toggles -- which is independently what CLAUDE.md
  recorded as "the blinking caret" in the multiplayer A/B, arrived at from the
  other end.

- **Field `0x0038` is the widget's SPRITE, and that improves an older
  comment.** `WidgetPaint` draws it and reads its bounds to centre it, so it
  is an `AM2_Sprite *`. `WidgetRepaint`'s walk up the parent chain -- written
  a few commits ago as "the first ancestor with `0x0038` set" -- is really
  "the nearest ancestor that has a backdrop to repaint over", which is a
  reason rather than a field test.

- **`flag3C` centres the sprite**, and that is why mutating it in
  `WidgetConstruct` showed nothing: the base constructor's 1 is not what the
  buttons on screen are using. Whether a subclass overwrites it is not yet
  established -- worth a probe rather than another guess.

- **The widget layer's remaining pieces.** All five vtable slots have a
  reconstructed base now, and the two forwarding thunks with them, so every
  class in the array reaches OUR code through every slot it does not override.
  What is left is per-class: the 33 constructors, which is where the subclass
  tails get their meaning, the per-class painters that are not the shared one,
  and the edit box at `0x00454C10`, which owns `g_charHandler` and is the one
  with a text buffer to get wrong.

- **The two focus walkers disagree about what "eligible" means.** Forwards
  (`0x00453DB0`) requires `0x0050` set AND `0x004C` clear. Backwards
  (`0x00453E20`) looks only at `0x0050` and never reads `0x004C` at all. So a
  widget with `0x004C` set is skipped going down and landed on going up.
  Reproduced rather than reconciled: nothing in the shipped menus has been seen
  to set `0x004C`, so which of the two is the bug is not established -- only
  that they differ. Worth a probe that reads `0x004C` across a live dialog.

- **UP is now driven too, so all five of `WidgetUpdate`'s branches are.**
  DOWN, DOWN, UP on the OPTIONS menu leaves the highlight on CONTROLS -- net
  one down from AUDIO -- and `WidgetTakeFocus` climbs to 5, which is the
  cross-check that the walkers actually dispatched slot 3 rather than merely
  running. The walkers' own counters read 0, because `WidgetUpdate` is ours and
  calls them directly.

- **Four of `WidgetUpdate`'s five key branches are confirmed by DRIVING, not
  by pixels.** With the OPTIONS menu up and 408,272 calls on the clock:
  DOWN moves the highlight AUDIO -> CONTROLS -> DIFFICULTY (721 pixels, twice,
  the same signature each time), TAB moves it identically (721 pixels, same
  bounding box), SPACE opens the CONTROLS dialog (305,916 pixels), and RETURN
  on DIFFICULTY opens SELECT DIFFICULTY. UP is the only one not driven, and it
  is the only branch with a callee the others do not share.

  This is the strongest evidence available in this project and it costs one
  run. A reconstruction that CAUSES a transition is checked by driving the
  input and seeing where the game ends up -- no second run, no budget, and it
  discriminates a wrong scancode constant, which no pixel comparison against
  an identically-driven original ever could.

- **Three more fields fell out of the two focus walkers.** `0x002C` is the
  PREVIOUS sibling, so the child list is doubly linked; `0x0050` clear and
  `0x004C` set each disqualify a widget from taking focus; and `0x0054` is a
  cdecl activate handler fired by SPACE or RETURN on release. `0x0050` is the
  flag whose mutation `controls` could not see -- it is read by the focus
  walkers, which that configuration never reaches.

- **ESCAPE closes the CONTROLS dialog, through our code, and that is the best
  evidence in this layer so far.** `WidgetUpdateCancel` runs 73,393 times with
  the dialog up -- once per widget per frame -- and tapping ESCAPE takes it to
  77,370 and returns the screen to the OPTIONS menu. So the branch that was
  reconstructed from reading `!IsKeyDown(1) && KeyChanged(1)` as "the key was
  RELEASED" is the branch that produced a visible state change. Not a pixel
  comparison and not a counter: the game did the thing.

  Worth remembering as a technique. A reconstruction that CAUSES a state
  transition can be checked by driving the input and looking at where the game
  ends up, which is stronger than any budget and needs no second run.

- **`tools/ab.sh controls` still closes with the CANCEL button, not ESCAPE.**
  Both reach the same handler; the click also exercises the button widget's
  own path, so it is the better of the two to automate. ESCAPE is the probe.

- **`LabelConstruct` 21, `LabelDestruct` 21, and that is an invariant.** Every
  label the CONTROLS dialog builds is destroyed when CANCEL closes it, exactly.
  It is the registry invariant's shape -- two counters that must agree or
  something leaked -- and it is the first one this project has in the menu
  layer.

- **The labels are MEMBERS, not heap children, and the counters say so.**
  `LabelDelete` and `WidgetDelete` -- the two MSVC scalar deleting destructors,
  vtable slot 0 -- read **0** while `LabelDestruct` reads 21. A heap child
  destroyed through `WidgetDestruct`'s walk would go through slot 0 and move
  both. Twenty-one calls arriving at the destructor ADDRESS while the deleting
  wrapper is never entered means original code is calling the destructor
  directly with no free, which is what MSVC emits for a member object whose
  container is going away.

  So the child-walk in `WidgetDestruct` is not what takes these down, and
  slot 0 stays verified by reading.

- **`tools/ab.sh controls` closes the dialog now** and compares three frames:
  the OPTIONS menu mid-sequence, the open dialog, and the screen after CANCEL.
  The dialog frame is exact. Closing it is the only thing in the suite that
  destroys a widget at all.

- **`controls` samples two frames now, and it did NOT fix what it was built
  to fix.** `ab.sh` takes a shot between the two clicks as well as after them,
  and the comparer checks every frame a configuration leaves behind. The mid
  frame is genuinely discriminating -- a width-from-height slip in
  `WidgetScreenRect` is 93,347 pixels there, independently of the 305,939 on
  the final frame -- so a defect that only touched the OPTIONS menu would now
  be caught where before it could not be.

  But it does not catch the three mutations that motivated it. Focusing the
  obvious widget instead of the parent's first child still passes both frames.
  So the reason those pass is NOT that the sample was too late; the focus and
  flag state simply does not change what either screen looks like. They stay
  verified by reading, and the next idea has to be a different one -- a probe
  that reads the widget tree over the control socket would settle it directly,
  where more screenshots will not.

  **Extending a test is not the same as extending its reach**, and the second
  frame had to be mutation-checked on its own before it counted for anything.

- **`WidgetTakeFocus` teaches the type of field 0x0034.** It stores `this` and
  `firstChild` there and dispatches a vtable slot on what it reads back, so it
  is a widget pointer -- `focusedChild` -- and not the `int32_t` the struct
  had. Reconstructing a function is how the struct gets learned; the fields
  were named from the constructor, which only ever writes zeroes and cannot
  say what a zero is.

- **The widget vtable has five slots and they are all named now.** Reading all
  33 vtables at once is what did it -- slot 3 is the same function in 30 of
  them and slot 4 in 29, so those are the base's and the rest are overrides.
  0 destructor (all distinct), 1 paint (`0x00454BA0` in 18), 2 click
  (`0x00454BD0` in 17), 3 focus (`0x00454070` in 30), 4 repaint
  (`0x00453FF0` in 29).

  Slot 2 was recorded as "click" for one commit and is the per-frame UPDATE.
  Its three input queries are `IsKeyDown`, `KeyChanged` and a consume, the
  scancode is 1 -- ESCAPE -- and `!down && changed` is a RELEASE, so
  `0x00454BD0` is the base update with a cancel key in front of it. For the
  LABEL, `0x0060` is the ink colour byte rather than the handler, and the
  label's vtable has the base update in slot 2 rather than this one, which is
  what makes that safe and is the clearest evidence the subclasses lay their
  own tails out independently.

- **`WidgetRepaint` runs 3 times on the CONTROLS dialog and its interesting
  branch runs 0 times.** Making it never defer to an ancestor leaves
  `controls` at 0 pixels, so the walk up the parent chain is not taken on this
  path at all; it stays verified by reading. Clicking CUSTOMIZE CONTROLS
  lights the button and leaves the counter at 3, so whatever repaints a
  hovered button is not slot 4.

  That is the next thing to find, and it matters for the whole layer: 1.5
  million `WidgetScreenRect` calls and 78,174 paints are reaching the screen
  through a route that is not the repaint virtual.

- **`ADDR_FONT_SURFACE` and `ADDR_BACK_SURFACE` are gone, and they were both
  wrong.** `0x004FE08C` is the back buffer -- `InitDirectDraw` takes it off the
  primary with `DDSCAPS_BACKBUFFER`, the lock target starts pointing at it and
  `PresentFrame` blits it to the primary -- and it was named for the one thing
  font.cpp does with it. `0x00503100` is the offscreen map surface and was
  called BACK_SURFACE. Both comments in `orig.h` already said the name was
  wrong and that it had been kept anyway because it was spread about.

  **A comment saying a name is wrong is not a correction; it is a note that
  the correction was declined.** Renaming both cost one `sed` and took the
  five `g_` names on the back buffer -- `g_back`, `g_backBuffer`,
  `g_backBuffer2`, `g_backBufferSurf`, `g_fontSurface` -- down to one. The
  `checkglobals` alias backlog went 38 to 34 and `checkpatches`'s `ADDR_`
  count stayed at 31, which is what renaming rather than aliasing looks like.

- **The `checkglobals` backlog is 28 + 15, down from 38 + 17.** Three of the
  four worst entries are done: the back buffer (five names), the draw target
  (four) and the primary surface (four) are one name each. What is left is
  smaller and mostly const-vs-non-const. `ADDR_HWND` through three is the next
  one with an actual claim in it -- one of the three is `g_enumContext` in
  `dplay.cpp`, which says something quite different from "the window".

- **`tools/checkglobals.py` is in, and the backlog started at 38 + 17.** Nothing had
  ever checked the `g_` macros. The first run found 38 surplus names for
  addresses that already had one and 17 names carrying more than one spelling.
  It is a ratchet at those figures -- lower them, never raise them.

  Worth working off in order of how much a name is claiming:

  1. ~~`ADDR_FONT_SURFACE` through five~~ -- done, and the name was wrong too.
  2. ~~`ADDR_LOCKED_SURFACE` through four~~ -- done. Both `SetDrawTarget` and
     `LockSurface` write it, so "currently locked" was half a name; it is
     `ADDR_DRAW_TARGET`.
  3. `ADDR_HWND` through three, one of which is `g_enumContext` in
     `dplay.cpp` -- a name that says something quite different from "the
     window". ~~`ADDR_PRIMARY_SURFACE` through four~~ is done.
  4. The remaining drifts are mostly const-vs-non-const and cosmetic. One is
     not: `g_screenClip` is the ADDRESS in one module and the OBJECT in
     another. A single spelling across the split is impossible for a COM
     pointer -- a flat module may not name `LPDIRECTDRAWSURFACE` -- so a
     couple of these drifts are structural rather than sloppy, and the ratchet
     should not be expected to reach zero.

- **Two of the base widget's constructed flags are not observed at all.**
  `WidgetConstruct` writes 1 into `0x003C` and 1 into `0x0050`; setting EITHER
  to 0 leaves `controls` at 0 pixels. So the exact A/B that catches a
  one-colour error in `LabelDraw` and a width-from-height error in
  `WidgetScreenRect` says nothing whatever about those two fields, and they
  stay verified by reading. Executing is not covering, on a screen where
  almost everything else is.

  Worth chasing later with a probe rather than more mutations: find where
  either field is READ. `0x0044` is read by `0x00453E80` and written by
  `0x00454070`, so that one at least has a known consumer.

- **`g_defaultOwner` was defined four times over three types; it is one now.**
  `objtable.h` had it as `uint32_t`, `audio.cpp` redefined it as
  `const uint32_t`, `dplay.cpp` as `int32_t` -- and again as
  `g_defaultOwnerSlot`, a second name for the same address. Same hazard as the
  local typedef that hid `PlaySoundAt`'s two-pointer compare, and an alias of
  exactly the kind the `ADDR_` ratchet stops one level up, where something is
  watching. The build now compiles with no warnings at all.

  Nothing checks this. `checkpatches.py` ratchets `ADDR_` aliases and
  `checkseams.py` ratchets `orig_` lies, but a `g_` macro can be redefined in
  as many modules and as many types as anyone likes, and only GCC objects --
  and only when two of them meet in one translation unit. `g_defaultOwnerSlot`
  never did meet the others, so nothing said a word about it.

- **`WidgetScreenRect` is the busiest thing in the tree: 1,510,864 calls.**
  Eighty bytes, thirty-three callers, and the shared base helper the whole
  menu hierarchy places itself with. It is also thoroughly CHECKED and not
  merely run -- taking the width from the height field puts `controls` at
  **305,939 pixels** of 786,432, 39% of the frame.

  Ratio worth keeping: 1,510,864 against `LabelDraw`'s 135,490, so roughly
  eleven placements per label draw. The menu repaints far more widgets than it
  paints text on.

- **The rest of the label class is the obvious next unit**, and all of it is
  covered by `controls`: the constructor at `0x00454E70`, the destructor at
  `0x00454EF0` and the scalar deleting destructor at `0x00454ED0`. After that
  the shared base virtuals, which pay for themselves across many classes at
  once -- `0x00454BA0` is slot 1 for about fifteen of them and is a 48-byte
  forwarder, `0x00454070` is 128 bytes, and `0x00453E80` is 496 with 21
  callers and two vtable dispatches in it.

- **The menu widget layer is 33 classes and one of them is now ours.** The
  image lays out thirty-three FIVE-slot vtables end to end from `0x0046FAB8`
  to `0x0046FD38`, each with exactly one constructor and one destructor storing
  it. `LabelDraw` (`0x00454F00`) is slot 1 of the twenty-sixth. That is a whole
  subsystem sized before any of it was read, and `src/game/win32/widget.cpp` is
  where it goes -- the four other virtuals per class, the containers, and the
  edit box at `0x00454C10` that owns `g_charHandler`.

- **`tools/ab.sh controls` is in; its budget is 200 and was briefly 0.** Two
  clicks from the title screen, no typing and no mission, 78,174 `LabelDraw`
  calls. The dialog is exact. The CURSOR is not: about one run in five it
  differs by ~45 pixels inside a 10x13 box wherever the last click left the
  pointer, and three consecutive runs at 0 were enough to convince me it was
  deterministic and not enough to be true. 200 covers the box; the errors this
  screen exists to catch are thousands of pixels.

  It discriminates: clearing the label background with the ink colour instead
  of the paper colour puts it **17,110 pixels** over. Nothing else in the suite
  compares the menu widget layer at all -- `bootcamp` and `campaign` pass
  through the menus on their way somewhere, and the game composes no frames
  while a dialog is up.

- **`am2.Image.refs_to` cannot see a call.** It scans for the address as a
  dword, which finds vtable slots and `push imm32` and nothing else; `call
  rel32` stores a displacement. It answers **0** for `LockSurface`, which has
  38 call sites. Believing it produced a survey saying not one of the 33 menu
  widget classes is ever instantiated -- nonsense that is one screenshot away
  from being disproved, and it was two minutes from being committed as a
  "dead code" finding. `am2.Image.xrefs` decodes and is the one to use.

- **The 400 appends were one loop, and the counter proves it.** Converting
  `dplay.cpp`'s `orig_msg_add` seam to a direct call -- which `checkseams.py`
  demanded the moment `0x00401050` was patched -- took `MsgListAdd` from 400 to
  **0** in one step, with the code behaving identically either way. So the whole
  400 was the pool fill in `CommCreateDirectPlay`, not traffic. This is the
  count-of-0 blind spot manufactured on purpose rather than stumbled into, and
  it is the cheapest way there is to find out which caller a count belongs to.

- **400 appends and the linkage is still unobserved.** `MsgListAdd` runs 400
  times on a campaign drive, which by call count is the busiest thing taken in
  a while -- and breaking the list's forward link entirely leaves `campaign`
  clean. Single player appends to the comm message list and never WALKS it, so
  the structure the function maintains is never read. Another case where a high
  call count says nothing about coverage.

- **Three more import sites are ours, reached through the game's own IAT.**
  `WaitForSingleObject`, `ReleaseMutex` and `PostMessageA` are called through
  their slots rather than by importing the symbols into `am2hook.dll`, which
  keeps `msgslot.cpp` on the flat side of the split -- the handle is an opaque
  pointer and no Win32 type is named. `docs/boundary.md` moves 56 -> 58
  functions and 160 -> 163 sites; "still boundary" stays at 3 and 6, the
  unreachable CD dialogs.

- **The .aai files contain no floating-point numbers either.**
  `DefParseNumber` runs 553 times and `DefParseFloat` -- its strtod twin, with
  the identical shape and complaint -- runs 0. Taken with the earlier finding
  that forcing strtol to base 10 changes nothing, the shipped `.aai` corpus
  uses plain decimal integers throughout and exercises neither the alternate
  bases nor the float path.

- **What is left in event.cpp is the dense half, and the shim run is over.**
  Ten functions remain: the 4096-byte action executor at `0x00420410`, a
  448-byte helper at `0x00421590`, and eight in the 160..192 byte range that
  are NOT more shims -- `0x00420260` and `0x00420300` each open with a
  two-step lookup through `0x00459FB0`/`0x00459FE0`, build a struct on the
  stack, and reach five or six unnamed callees apiece. Read one before
  budgeting for it; the pattern that made the last dozen quick does not hold
  here.

- **"Clear field 0x540 first, but only for type 2" is a recurring step.** Three
  sightings now: `EvtAtPointA`, `EvtObjPair`, and `EvtSetField540` which exists
  to write that field directly. Several actions reset it before giving an
  object something new to do, so whatever it holds is per-order state rather
  than per-object identity. Expect the step in anything that redirects a type-2
  object.

- **Pause reason 8 is "a full-screen bitmap is up", and that names a bit of the
  pause mask.** `EvtShowBitmap` calls `SendGamePause(1, AM2_EVENT_FLAG_8)`
  where frame.cpp calls `SendGamePause(0, AM2_EVENT_FLAG_8)` -- set and clear
  of the same reason. CLAUDE.md records the event flags AS the pause mask
  without naming any bit; this is one named.

- **`showbitmap` selects sub-state 22 and marks the overlay dirty, which
  confirms CLAUDE.md's reading of that table from a caller.** 0x16 is 22, which
  is `AM2_SUBSTATE_BASE` -- the first of the thirteen arms, and one of the nine
  described as "repaint if the overlay is dirty, then DrawMenuOverlay". The
  companion write is to `ADDR_OVERLAY_DIRTY`, already named, which is exactly
  that dirty bit. The two writes are one gesture; the alias ratchet is what
  made them legible, by refusing a second name for the flag.

- **A pair that looks symmetric can be crossed, and drawing the table is what
  shows it.** `EvtFlag40Clear` and `EvtFlag40Set` each have an ordinary arm and
  an ID15 arm, and across those four cells the bit is SET twice and CLEARED
  once, with the fourth cell touching no object at all. Reading them as
  "one sets, one clears" -- the obvious shape for a pair sharing a bit -- would
  be wrong in three cells of four. Writing the two-by-two out before coding is
  cheap and settled it.

- **"Unreachable" was true of the id and false of the name.** `ADDR_SVAR_ID15`
  carried the comment "unreachable", which is correct about the RESOLVER's jump
  table -- no keyword produces id 15. Two functions, `0x0041F570` and
  `0x0041F5C0`, compare a name index against that global directly and take a
  special path when it matches, so the global is live. The comment is corrected
  rather than removed: both halves are worth knowing, and a bare "unreachable"
  invites skipping the functions that use it.

- **A redefinition warning caught a name that was a guess AND a clash.**
  `AM2_OBJ_STATE_REC_SIZE` already meant `AM2_ObjState`'s sixteen bytes; I
  reused it for a 256-byte record in an unrelated table, on the strength of
  calling those records "state" without evidence. GCC said `redefined` --
  audible only because that warning stopped being filtered several commits ago
  -- and the fix was both to rename and to stop claiming what the records hold.
  Two mistakes, one warning.

- **Object kinds 2 and 3 get identical treatment differing by one field.**
  `ScriptSetObjTable` writes `+0x4C0` for kind 2 and `+0x4C8` for kind 3 inside
  the sub-record at `obj+0x6C`, then propagates the same value through
  SetFieldInAll for both. Two of the three kinds CLAUDE.md lists as
  unidentified, and that they are this close together is a fact about them
  worth keeping.

- **Name a function from the body at the OTHER end of its calls too.** The
  alias ratchet refused `ADDR_COND_LOCAL` on `0x00421890`, which already held
  `ADDR_SCRIPT_FIND_FILE`. Reading the caller alone -- a two-armed branch on
  ADDR_MP_SESSION -- suggested "the single-player way of handling a condition".
  The callee's own "%s%d.txt" says it is the mission-script loader, so the
  caller is the end-of-mission router and is named `AdvanceMission` instead.
  The project rule has been "read the body, not the call site"; this is the
  same rule applied downward, and the ratchet is what forced the check.

- **"A zero x means use the object's own position" is a convention of this
  codebase, not a coincidence.** Three functions now use it -- `EvtDeployItem`,
  `ScriptResurrectItem` and `ActionPoint` -- and in every one the test is on
  the LOW WORD alone, so a point with x == 0 and any y counts as absent. Worth
  expecting in anything that takes a packed point.

- **Executing a function is not covering its branches, and the count says so.**
  `ActionPoint` runs 6 times and has three ways to produce a point; removing
  the object-position fallback entirely leaves `campaign` clean, so all six
  calls take the literal path. The variable path cannot even be measured from
  the counters, since `GetVarValue` is ours and reached directly.

- **The event system's authority rule and its off switch are one function.**
  `EventNotify` refuses twice before dispatching: in a multiplayer session only
  the HOST raises anything, and nothing is raised at all while the in-mission
  sub-state is 34 -- the ESCAPE arm CLAUDE.md records ordinary play as never
  being in. So entering that menu stops the event system rather than merely
  pausing the frame, which is a stronger statement than the pause mask makes.

- **A delayed event carries less than an immediate one.** `EventTriggerDelayed`
  takes no masks and no second num/uid pair, so `EventNotify` DROPS four of its
  ten arguments on that path. `delay 0` and `delay 1` are not the same event
  arriving at different times. The `delay > 0` boundary is exact and observed:
  making it `>= 0` puts campaign at 294,304 differing pixels.

- **A log string that made no sense alone is explained by two functions
  together.** `DeployItem`'s own line reads "DeployItem(resurrection)", which
  looked like a mis-copied message when only `EvtDeployItem` had been read --
  that one passes 0 for the third argument and has nothing to do with
  resurrecting. `ScriptResurrectItem` passes 1 for the same argument. So the
  flag is the resurrect flag and the string is accurate; neither caller
  explains it, and both together do.

- **`0x0041F4A0` has 26 callers and is the densest thing left in the module.**
  128 bytes, gated on two globals and a state compare against 0x22, then one of
  two six-argument calls into `0x0041F410` depending on whether an argument is
  positive. Worth doing next on call-count alone.

- **`EvtArmyAtPoint` ships an accumulating offset, and it is reproduced.** With
  `relative` set it copies the point into two registers before the loop and
  never reloads them, so the second matching object receives
  point + first.pos + second.pos and the third gets all three summed. The loop's
  back edge landing after the initialisation is what settles it -- reading the
  body top to bottom suggests a fresh copy each time. Nothing in this port may
  quietly fix a bug the game ships; a mission that happens to rely on the first
  object's offset would change behaviour if it were.

- **A `ret 4` is what makes surrounding stack arithmetic legible.**
  `CommSlotForArmy` is thiscall -- comm object in ecx, one stacked argument
  cleaned by the callee -- and until that was checked, the `mov [esp+8], eax`
  after the call appeared to overwrite the return address. Check the callee's
  epilogue before concluding a caller is doing something impossible.

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

- **10 functions are left in the event.cpp band, and most are tiny.** Four
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
