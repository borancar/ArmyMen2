# Status

A dated snapshot of where the reconstruction is, kept so a session does not
have to re-derive it. **`CLAUDE.md` and `docs/` are authoritative**; this file
is a summary and can be stale between updates. Every number below carries the
command that produces it, so it can be re-measured rather than believed.

Last updated: **2026-08-22**, at `1b0b82f`. Working tree clean.

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
| `patch_replace` sites | 517 | `grep -rho patch_replace src/game \| wc -l` |
| distinct addresses reconstructed | 517 | 510 of them below the CRT line |
| sub-CRT functions in the image | 1,239 | `docs/functions.tsv` |
| sub-CRT code reconstructed | 97,024 / 372,816 B (**26.0%**) | `tools/reconstructed.py`, split at referenced starts |
| the same, crediting whole entries | 121,712 / 372,816 B (32.6%) | what every earlier session quoted, and an over-count |
| modules | 28 flat + 16 `win32/` | `tools/checkclaims.py` |
| pure unreconstructed leaves | **0** (2 listed, both false positives) |
| self-naming unreconstructed functions | 109 at the sweep, 10 taken since | `tools/vectors.py --all` |
| boundary functions reconstructed | 68, 179 import sites | `docs/boundary.md` |
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

   - The GAME SELECT PANEL's LOAD arm (`0x00452060`) fires: `0x00511B88`
     holds `"map1_mission1.sav"`, `0x00511A68` holds `"sarge"`.
   - Mission start (`0x00425300`) takes the LOAD branch, so `0x00511DD8` was
     set when it read it at `0x00425360`.
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
