# The Win32/DirectX boundary: the reasoning behind the numbers

`docs/boundary.md`, `docs/comcalls.tsv`, `docs/imports.tsv` and `docs/crt.md`
are GENERATED and carry the figures. This carries the reasoning, which is not
derivable from them and which took several wrong turns to arrive at -- what
counts as boundary work, what the inventory can and cannot SEE, why the CRT
line is a rule of thumb, and why every "declined on density" function was
eventually reconstructed anyway.

Read the numbers from `docs/boundary.md`, never from prose. This file states
that repeatedly because quoting a generated number in prose is how three
separate figures in `CLAUDE.md` went stale.

The one-line summary: the import side and the COM side are both DONE, in the
careful sense that every Win32 call site in the image that can actually execute
is either inside reconstructed code or incidental, and all 207 confirmed COM
dispatch sites below the CRT are inside reconstructed functions. What is left
outside is game logic holding a handle it did not make.

  the code, and neither drift was visible without checking both.
- **A vtable call is only COM if `this` is pushed.** Under `CINTERFACE` every
  COM method takes the interface as an explicit first argument, so it goes on
  the stack; an i386 MSVC C++ virtual is thiscall and keeps `this` in `ecx`.
  The two compile to the same `mov vt,[obj]` / `call [vt+N]` pair, so a survey
  that matches only on shape reports the engine's own destructor chains as
  DirectX — and the densest-looking candidates, 48 bytes of nothing but vtable
  calls, are exactly those. `push 1` into slot 0 is the clearest tell: that is
  the MSVC scalar deleting destructor, and COM's slot 0 is `QueryInterface`,
  which takes three arguments. `tools/comcalls.py` records this as its `abi`
  column; **145 of 353 in-game sites are C++ rather than COM**.

  That number was 90 until the classifier learned its second tell. It looked for
  `push <obj>` (COM) and for the object already being in `ecx` (C++), and gave
  up on anything else — but a virtual method that takes ARGUMENTS pushes those
  arguments and then does `mov ecx, <obj>` immediately before the call, so it
  showed both patterns and landed in `?`. That bucket was 56 sites, 45 of them
  in `script.cpp..unit.cpp`, i.e. the engine's own object model.

  The rule that resolves it: whichever appears CLOSEST to the call wins, because
  that is the one establishing the convention — COM pushes `this` last, thiscall
  loads `ecx` last. `?` went from 56 sites to 1, and **`stdcall` did not move at
  all**, which is the check that matters: the change only reclassified unknowns
  and took nothing out of the COM set. The last `?` went later still, when the
  cdecl rule identified it as a linked-list callback, so the column now reads
  `stdcall`, `thiscall` or `cdecl` for all 356 with nothing unknown.
- The Win32/DirectX boundary is inventoried and being worked outward-in: 122
  functions below the CRT touch the import table (`docs/imports.tsv`) and 76
  contain genuine COM dispatch (`docs/comcalls.tsv`) — that second figure was
  110 before the ABI classifier was fixed, and the 34 that left were never
  boundary code at all: 33 were the engine's own C++ virtuals and the
  thirty-fourth was the cdecl callback. Done so far: `WinMain`,
  `InitApplication`, `PumpMessage`, `PositionWindow`, `WndProc`,
  `InitDirectDraw`, `InitInput`, `CreateOffscreenSurface`, `ClearSurface`,
  `RealizeSystemPalette`, `SnapshotSystemPalette`, `ReportError`, `FatalError`,
  the three `Wave*` helpers, both DirectPlay creators, the two bitmap loaders
  (`CreateBitmapSurface`, `ReloadBitmapSurface`), `RestoreTileSet`,
  `OpenAudioStream`, `AudioTimerProc`, both input pollers, `ComposeFrame`,
  `ScrollView`, `ScrollMapCache`, `CommEnumPlayers`, `HostBattle`,
  `SetGamePalette`, `DrawMenuCursor`, `StartPacketThread` and the comm
  object's constructor and destructor. The window, the message queue, the display mode,
  every surface, both input devices, the GDI palette, all `.WAV` reading,
  sprite upload from a stream, the whole network transport and the entire
  registry surface are ours.

  Do not read the leftover as work outstanding, and **read the figures from
  `docs/boundary.md` rather than from this paragraph** — the ones that used to
  be quoted here (87 functions, 83 game logic, 4 and 13 outstanding, naming
  four addresses) were stale by many commits, which is what quoting a generated
  number in prose always comes to. Of the 80 import-touching functions not
  reconstructed, 77 are game logic — a `GetTickCount` or a `PostMessageA`
  inside something that is otherwise not boundary at all — leaving 1 function
  and 2 sites, a `MessageBoxA` and its `GetActiveWindow` inside menu code that
  no branch in the image can reach. The channels
  themselves are owned: every DirectX object in the process is created,
  configured and destroyed by reconstructed code, and the registry is opened
  and closed by ours. What still dispatches through COM is game logic holding a
  handle it did not make.
- **Second instance, and the cure was doing the same arithmetic elsewhere
  first.** `CreateTrooper` was deferred with a reading that could not be made
  self-consistent: `BuildRowSet`'s `dy` appeared to come from argument 2's
  slot, which by then held a packed point, making every trooper's row y equal
  its x. The blocker was an esp off by five pushes -- `RectSet`'s arguments and
  the `lea` below them -- and `dy` is argument 3 with no aliasing at all. What
  made it followable was writing `CreateExplosion` and `PlacementScreenClick`
  first, both of which really DO reuse a slot for a packed point: in both, the
  point lives in a LOCAL that a bare `push ecx` reserved. Knowing what the
  shape looks like when it IS there is what made it readable when it was not.

**A decline is worth revisiting when the reason was uncertainty rather than
  scope.** `0x0040BCF0` sat on the list below for most of a session because its
  position fields looked like they aliased — `[eax+0x12]` on one path and
  `[eax+0x10]` on another, apparently two overlapping fields of one record. They
  are not the same record: a lookup call between them reassigns `eax`, so one is
  the game object's position and the other the sound's, and both are plain
  `AM2_Point`. Reading it a second time took minutes. "I could not follow this"
  ages differently from "this is game logic"; only the second is a decision.
- **Do not trust a one-line label on this list, including the ones below.** Six
  entries have now been found mislabelled and then reconstructed:
  `Update3DAudioVolumes` ("distance model, DirectSound only executes"),
  `StopNamedSound` ("compares names byte by byte"), `MakeBitmap` ("sprite cache
  management"), `DrawMenuOverlay` ("map object placement"), and before them
  `AttachPalette` and `ADDR_AUDIO_CHECK_PATH`. Every one was written from a
  glance at a call site rather than from the body, which is the failure this
  file already warns about under "name a function from its body".

  The cheapest antidote is to let a function name itself: most of the ones that
  matter carry their own error strings. Sweeping the candidates for pushed
  string literals is a minute's work and it is how `MakeBitmap`,
  `loadtileset` and `RestoreTileSet` were identified.

- **Read and deliberately left original.** These come back to the top of every
  candidate ranking, so they are listed here rather than re-read each time.

  | | what it actually is |
  |---|---|
  | ~~`0x0042BEA0`~~ | **Done.** The entry was four functions; `RestoreTileSet` at `0x0042C0E0` (624B, `Lock`+`Unlock`) held both COM calls and is reconstructed in `mapdraw.cpp`. See the merge note under boundary density |
  | ~~`0x0040CED0`~~ | **Done.** Two functions: `AudioTimerProc` at `0x0040D020` (1456 B, the streaming refill) and `OpenAudioStream` at `0x0040CED0` (336 B, opens the `.WAV` and creates the buffer). Both reconstructed; the whole audio stream is ours |
  | `0x00412FE0` 1184B, 4 | menu logic; no strings |
  | `0x0042FF60` 448B, 1 | starts a multiplayer game — it calls `CommOpenSession`, `CommCreatePlayer` and `PlaySoundAt`, all of which are ours. Genuinely menu logic |
  | ~~all of them~~ | **Done.** Every entry that was ever on this list is reconstructed — the last were `SetGamePalette` (`0x0041B0E0`) and `DrawMenuCursor` (`0x00412FE0`) |
  | `0x00453BC0` 48B | not COM at all — a C++ destructor chain, per the `abi` note above |

- **Ask what the inventory can SEE, and `docs/boundary.md` now answers that
  first.** Its opening table lists every mechanism this image has for reaching
  outside itself — named imports, imports by ordinal, COM vtables, runtime
  resolution through `LoadLibraryA`/`GetProcAddress`, delay-loaded imports —
  and what is outstanding on each. The zeroes are not the point; the list is.
  `tools/comcalls.py` exists because an earlier `coverage.py` could not see COM
  at all and called the boundary nearly finished with 23 functions and 66
  DirectX calls outside it.

  Two of those rows needed care to be true rather than merely green. An import
  by ordinal appears as a one-instruction thunk that lives with the CRT, so
  counting its recorded site answers about the thunk and not about anything
  using it — `DSOUND.dll #1` is `DirectSoundCreate` and what matters is that
  its only caller, `InitDirectSound`, is ours. And runtime resolution is a real
  channel here: `DetectCpuSpeed` loads `cpuinf32.dll` and calls `wincpuid` and
  `cpunormspeed` through pointers no static scan can follow. It is
  reconstructed, so the channel is ours, but nothing about an import table
  would have told you it existed.

- **"Incidental" is a judgement, and it was wrong about threads.** The claim
  that the import side is finished rests on a hand-kept list in
  `tools/coverage.py` of symbols that are a fact of running on Windows rather
  than a channel out. `CreateThread`, `CreateEventA`, `CreateMutexA`,
  `SetThreadPriority` and `CloseHandle` were on it, which meant `0x004021A0` —
  which creates an event, starts the comm thread and sets its priority — was
  being dismissed as game logic, while CLAUDE.md separately called that cluster
  genuinely boundary. Both cannot be true.

  The line now is the one this project already draws for DirectX: **creating or
  destroying an OS object is boundary work, operating on a handle you were
  given is not.** `docs/boundary.md` says of COM that what is left "is game
  logic holding a handle it did not make"; the kernel side has to mean the same
  thing or the word stops meaning anything. Waiting on a handle and releasing a
  mutex stay incidental.

  Correcting it moved the outstanding figure from 3 functions and 6 sites to 6
  and 11 — the extra three being the comm thread's mutex, event and thread.
  Those three are now reconstructed (`StartPacketThread`, `MsgListInit`,
  `EventClose`), so it is back to 3 and 6, and all six are the unreachable CD
  dialogs.

- **The import side is done, in the only sense the word can bear here.** Every
  Win32 call site in the image that can actually execute is now either inside
  reconstructed code or incidental — a `GetTickCount`, an `IntersectRect`, a
  mutex wait. What is left outside is one `MessageBoxA` call, and it sits
  behind a copy-protection check that has been patched to skip it.

  **It is a decision, and the number was three until the layer around two of
  them arrived.** `0x0042F290`, `0x0044D2E0` and `0x0044D3F0` each hold exactly
  two import sites — a `MessageBoxA` and the `GetActiveWindow` it passes as
  owner — and no COM dispatch at all. Everything else in them is menu logic:
  sound requests, menu state, calls into other game code. Porting one to
  capture a dialog that cannot appear is the opposite of what ranking by
  boundary density is for, and that reasoning has not changed.

  What changed is that two of the three turned out to be the title screen's
  SINGLE PLAYER and BOOT CAMP buttons. They were reconstructed for their own
  sake, as part of finishing the menu, and the CD check came along through
  `src/game/win32/cdcheck.h` at no extra cost. **A function declined on
  density can still arrive because the layer around it did** — which is a
  better reason to re-read `docs/boundary.md` than any argument in prose.

  **AND THE THIRD ARRIVED THE SAME WAY, so the figure is 0 and 0.**
  `0x0042F290` is START A WAR, and it was written as part of finishing the
  multiplayer menu rather than because anything here changed its mind about
  density. All three declined functions came in through the layer around
  them, one at a time, and the decision was never revisited once. Worth
  keeping as the strongest version of the rule: a density ranking does not
  decide what gets reconstructed, it decides what gets reconstructed FIRST.

  **That last step is now proved rather than asserted**, and the proof needed
  two corrections to be worth anything. `tools/binpatches.py` checks each
  skipped `MessageBoxA` against every branch target and stored address in the
  image, and all five answer *nothing can reach this*.

  The first version asked "does anything point into the skipped span", which
  answers yes for `0x0040EE9D` — its span is re-entered at `0x0040EEE7`, which
  is *after* the dialog and cannot run it. The question has to be whether
  anything lands at or before the call. And the branch scan read raw bytes for
  `0x70`-`0x7F`, which finds other instructions' operands and invented a `jo`
  and a `js` into the second dialog's span; branches now come from decoded
  instructions. Data references still come from a raw scan, because a wrong one
  there only makes the check more conservative.

  **The DirectX object claim was false until the palette was reconstructed.**
  "Every DirectX object in the process is created, configured and destroyed by
  reconstructed code" read well and was wrong: `0x0041B132` is the image's only
  `CreatePalette`, it sits in `SetGamePalette`, and that function was original.
  The display palette was the one object the port did not make. It is
  reconstructed now and the sentence is true, but it was worth finding out that
  nobody had checked it.

  **The COM side is now finished too, in the same careful sense.** All 207
  confirmed COM dispatch sites in game code — every `stdcall` vtable call
  `tools/comcalls.py` finds below the CRT — are inside reconstructed functions,
  and the named-object figure is 35 of 35 with 0 calls left.

  There is no longer an unclassified site: `abi` is `stdcall`, `thiscall` or
  `cdecl` for all 356, and the outer bracket reads 79 of 79.

  **The cdecl rule counts, and a first attempt that only peeked was wrong.** A
  COM method is stdcall and cleans its own arguments, so an `add esp` after the
  call ought to mean cdecl. It does not on its own: these functions push cdecl
  arguments *around* their COM calls, so that cleanup routinely belongs to the
  enclosing one. `SetSurfaceColorKey`'s `SetColorKey` is followed by
  `add esp, 8` with three arguments pushed, and `ClearRegion`'s `Blt` by
  `add esp, 116` with six — a peephole reads both as callbacks, and with a
  little slack it also takes `PresentFrame`'s `BltFast` and `Restore` and
  `BlitMapBackdrop`'s.

  Counting the pushes that belong to the call and requiring the cleanup to be
  exactly four bytes each settles it. `0x0041F060` pushes eight and cleans
  0x20, so it is the linked-list callback it looks like; every COM call above
  mismatches and stays COM. The check that it is safe is that reclassifying it
  moved exactly one line of `docs/comcalls.tsv` and left `stdcall` at 210.

  That figure moves less than the work does, and `ScrollMapCache` is the
  example: its `BltFast` reaches the surface through a stack slot, so
  `comcalls.py` cannot name the object and the call was never in the 5 to
  begin with. Reconstructing it closed a real DirectDraw call and left the
  headline number untouched. The `any COM dispatch` row went 74 to 75, which is
  where that kind of progress shows up.

  The outer bracket — every function with any unreconstructed COM dispatch —
  is now **8**, down from 42, because the classifier fix moved 33 pure-C++
  functions out of it. Ranked by density with merged sizes corrected there are
  12 real functions left and **not one is under 50 bytes per call site**; the
  densest is 146. By this project's own threshold there is no remaining COM
  function that is boundary code rather than game logic holding a handle.
- **A function address can arrive as `push imm32`, so an aligned-dword scan
  under-reports references.** Menu handlers in this binary are registered by
  pushing the function as an argument — `push 0x42ecf0; push 0x20; push 0x51`
  into a button constructor — and that operand sits wherever the instruction
  stream puts it, usually unaligned. A cross-reference scan that looks for
  rel32 branches plus *aligned* dwords will report such a function as having no
  references at all.

  This was not hypothetical: `0x0042ECF0`, the mission-start gate, was recorded
  in commit `44312d2` as dead code on exactly that mistake. It is a live button
  handler. Before concluding anything is unreferenced, decode the candidate hit
  and see whether it is the operand of a `push` — the byte before an
  address-shaped dword being `68` is the whole tell.

  **It has now been made twice, and the second time the scan was mine.** Asking
  whether `0x004256F0` was referenced, I checked `call rel32` and `push imm32`
  and concluded it was dead code — and it is reached by a `jmp` tail call from
  `0x004260C9`, which neither pattern matches. A reachability scan has to
  include every control transfer, not just the two that look like calls.

  Note this affects *function* reachability only. Whether a block inside a
  function can be reached is a different question and the answer there — that
  the five copy-protection dialogs are unreachable — was checked with a scan
  that did include unaligned operands, and stands.
- **This executable has been patched after compilation, and in more than one
  place.** Six conditional branches were overwritten with unconditional ones --
  `74`/`75` becoming `EB`, same length, nothing moved. Five disable the copy
  protection; the sixth removes the MULTIPLAYER entry from the title screen,
  which is the gap between SINGLE PLAYER and OPTIONS. `tools/binpatches.py`
  finds all six and `docs/binarypatches.md` gives the byte to restore each.

  The signature is a compare whose flags nothing reads, followed by an
  unconditional jump. The filter that makes the scan trustworthy is checking
  the jump's target: `cmp; jmp L` is ordinary when L starts with a `jcc`, which
  is what a loop back-edge looks like. With that, 16 candidates become 6 and
  all 6 are real.

  **`AM2_MULTIPLAYER=1` puts the button back**, via `src/inject/restore.c`, and
  that is the only way the reconstructed DirectPlay code can be exercised at
  all — the whole subsystem is otherwise unreachable. It is off by default
  because anything restored there makes the process differ from the binary it
  is derived from, which `tools/ab.sh` will correctly flag.
- **The copy protection in this executable is patched out, and that is not the
  same as absent.** `FindGameCD` is called from five places; all five branch on
  the result with `EB` (`jmp`) where a `75` (`jne`) has to have been, so the
  check always passes and the "insert the CD" `MessageBoxA` after it can never
  run. The tell is the `test eax, eax` left in front of each one setting flags
  nothing reads — no compiler emits that. `tools/binpatches.py` finds them and
  `docs/binarypatches.md` lists the one byte per site that puts each back.

  Two consequences worth keeping. Five of the six `MessageBoxA` sites that
  `docs/boundary.md` reports as outstanding are unreachable, so the leftover
  count overstates the work. And a reconstruction of any of those menu
  functions must reproduce the patched behaviour, not the retail behaviour, or
  it will fail the A/B against the original for a reason that has nothing to do
  with being correct — which is why `src/game/win32/cdcheck.h` records the retail
  check behind an `#ifdef` that is off.
- **The game has no networking imports at all** — no ws2_32, no wsock32, no
  dplayx, and not even those strings in `.text`. Its multiplayer transport is
  DirectPlay reached through COM, so the only trace in the import table is
  ole32's `CoCreateInstance`, twice. Both are now `src/game/win32/dplay.cpp`. Worth
  remembering when looking for a subsystem that seems to be missing: an absent
  import does not mean an absent channel.
- The remaining genuinely-boundary clusters are the mutex-guarded comm message
  list in `air.cpp` (`0x00401050` and friends — `WaitForSingleObject` and
  `ReleaseMutex` around a linked list, and multi-threaded, so a mistake there
  is a race rather than a crash), the Smacker movie class (`0x00444FC0`, all
  thiscall methods on a class whose layout would have to be reconstructed
  first), and the registry pair behind `0x0040DB50`.
- **The DirectX COM figure is a lower bound.** `comcalls.py` can name the
  interface only when the object traces back to a global, and it cannot for 182
  of the 356 dispatch sites — those reach it through a parameter or a struct
  field. `LockSurface` is the example: it takes the surface as an argument, so
  its own `Lock` is unclassifiable and was reconstructed long before the count
  existed. Read `docs/boundary.md` as "known to be outstanding", never as "all
  that is outstanding".
- **`CRT_START` is a rule of thumb, and every figure in the project rests on
  it.** All the tools count only functions below `0x0045C000`, which drops 138
  of the image's 414 import sites — an exclusion nobody had examined until
  `docs/boundary.md` was made to report it.

  It survives examination, but not in the way the constant implies. Above the
  line is genuinely the CRT — heap, locale, stdio, `RtlUnwind`,
  `SetUnhandledExceptionFilter` — which this port replaces with libc wholesale.
  But **game code lives up there too**: `DrawSeqBar` is at `0x004624A0` and has
  three `Blt` calls, so the boundary is not where the constant says. What makes
  the exclusion safe is not the address, it is that the only COM dispatch above
  the line is `DrawSeqBar`'s and that is reconstructed, and that the three
  DirectX entry thunks the linker parked among the CRT — `DirectDrawCreate`,
  `DirectInputCreateA` and `DSOUND #1` — are each reached only from
  reconstructed code.

  So "207 of 207 COM sites" was true and its denominator was quietly smaller
  than the image. Read it as "below the CRT line", and read the CRT section of
  `docs/boundary.md` for what that omits.

  **`tools/crt.py` now says exactly what is up there, and the constant is
  26 KB too low.** Game code runs to `0x00462600` — item and vehicle comm
  messages, `CreateWeapon`, `DrawSeqBar`, "Game type: %s" — and the CRT proper
  starts at `0x00464420`, with the six-entry thunk table parked in between.
  Above the nominal line are **112 game functions**, which is many more than
  this file used to imply by naming one.

  The exclusion still holds, and now for a measured reason. The entire outside
  contact of those 112 is two `GetTickCount` reads, three `IntersectRect`
  calls and one COM dispatch — `DrawSeqBar`, which is reconstructed. **106 of
  the 112 touch nothing at all.**

  Nothing is left unlabelled: 58 CRT functions are identified from their own
  body (import signature, or text like "Microsoft Visual C++ Runtime Library"
  and the `1#INF`/`1#QNAN` spellings, or in two cases arithmetic — MSVC's
  `rand` is the LCG `imul 0x343FD` / `add 0x269EC3`, and `_ftol` is the
  `fnstcw`/`fistp` dance), and the remaining 171 are CRT by sitting above the
  evidenced frontier. That list is what libc replaces on a native build.

- **The game never opens a file itself.** Every `CreateFileA`, `ReadFile` and
  `FindFirstFileA` is reached from inside the statically linked CRT, which this
  port replaces with libc wholesale rather than function by function. The one
  exception is the `.WAV` reader, which goes through WINMM — `wavefile.cpp`.
  `docs/boundary.md` generates that answer, because "where is the file I/O" is
  an obvious question with a non-obvious answer.
- **Import sites are only half the boundary.** DirectDraw, DirectSound and
  DirectInput are reached through COM vtables and own no import, so a function
  can call DirectX all day and appear nowhere in `docs/imports.tsv`.
  `tools/coverage.py` counted only imports for several commits and reported the
  boundary as nearly finished while 23 functions and 66 DirectX calls sat
  outside it. It counts both now. When adding a new kind of outward call, ask
  what the *inventory* can see before trusting what it says.
- `docs/boundary.md` answers "is the boundary handled yet" with numbers rather
  than prose, and regenerates from `tools/coverage.py`. It reads the
  reconstructed set out of the `patch_replace` calls themselves, so it cannot
  drift from what the harness installs. What is left outside is **0 functions
  and 0 sites**: the last one out was the `MessageBoxA` and `GetActiveWindow`
  pair in ADDR_ON_START_WAR, behind the fourth of the five disabled CD checks,
  reproduced the way `cdcheck.h` prescribes so the dialog stays unreachable
  exactly as the patched binary leaves it. The remaining sites are game logic
  that happens to read a clock or call `IntersectRect`.

  **Re-read those numbers from `docs/boundary.md`, never from here**, and
  expect them to move for reasons that are not progress. This bullet said "one
  function and four sites" until `tools/merges.py` learned to count unaligned
  references; the extra two functions were always outside, hidden behind a
  containment match against a merged neighbour. A number going *up* after a
  tooling fix is the tool getting more honest, not the work going backwards.
- **`obj -> table -> slot` with no `this` is a real shape in this binary, and
  it needs two dereferences.** `0x0065A058` (the repaint object) and
  `0x006568A0` (the current movie) are both reached as
  `mov ecx,[global]; mov eax,[ecx]; call [eax]`. Writing that as one
  dereference calls the vtable pointer as if it were a function, and the game
  exits instantly with nothing useful in the log. Cost an iteration; use a
  named local for the object and another for the table rather than a nested
  cast.
- **A coverage number that credits a whole merged entry is worth less than no
  number.** `tools/coverage.py` decided "is this reconstructed?" by asking
  whether *any* patched address fell inside the `functions.tsv` entry holding
  the site. Where the entry is two functions, reconstructing either one marked
  both done. It happened twice: patching `MovieApplyPalette` marked
  `MoviePoll`'s `SmackWait` covered, and reconstructing `AudioTimerProc` marked
  `OpenAudioStream`'s `CreateSoundBuffer` and `GetCaps` covered a commit before
  they were — so "24 DirectX calls left" was really 14.

  Sites are now attributed to the real function through `tools/merges.py` before
  anything is counted. Containment still applies *within* a real function, which
  is what keeps `WndProc` (patched at `0x0040A6B0`, filed under `0x0040A6A0`)
  working. The tool's own comment had predicted this failure and said "if this
  number ever looks too good, that is the first thing to check"; it looked too
  good and nobody checked.

- **Pick the next target by boundary density, not by import count.** Ranking
  what is left by sites-per-byte finds functions that are boundary code;
  ranking by sites alone finds 5,760-byte game-logic functions whose only
  contact with Win32 is a `GetTickCount`. Reconstructing one of those to
  capture a timer read is exactly what this port is not for. Everything under
  ~50 bytes per import site is worth looking at; above that, read it first.

  **But the denominator is wrong for one entry in eight.** `docs/functions.tsv`
  is built from a symbol-free image, and where it cannot see a boundary it runs
  neighbours together — at least 128 sub-CRT entries covering 90 KB, measured by
  `tools/merges.py`. A merged entry has the COM sites of one function over the
  bytes of several, so *both* halves of the ratio are wrong and they push it the
  same way: merged functions rank too low and get declined. The 5,760-byte
  function named just above is itself 17 functions.

  This is not hypothetical, and it cost a real target. `0x0042BEA0` sat on the
  declined list as "1200 B, 2 COM calls" — 600 B per call, far past the
  threshold. The entry is four functions; the one holding both calls is
  `RestoreTileSet` at `0x0042C0E0`, 624 B, a file-to-surface loader, and it is
  now reconstructed. Run `tools/merges.py --com` before ranking anything.

  **A tool that recommends targets has to know what is already done, and three
  separate ways of not knowing all bit within an hour of writing this one.** It
  first proposed `0x00445320`, which had been `MovieApplyPalette` for some time,
  because it ranked straight out of `comcalls.tsv` — a description of the
  ORIGINAL image, which has no idea what has been replaced. Then, once it read
  the patch list, it proposed a batch of `script.cpp..unit.cpp` virtuals,
  because it excluded only `abi == "thiscall"` and let the unclassified through
  as if unknown meant COM. Then it reported `WndProc`'s whole entry as
  outstanding, because **not every reconstruction is a patch** — that one is
  registered into the `WNDCLASS` and appears in no `patch_replace` call.

  The shape is the same each time: the tool was measuring the binary when the
  question was about the binary *minus what we have done to it*. Require
  positive evidence (`abi == "stdcall"`, not "not thiscall"), and subtract the
  reconstructed set by every route it can be installed through.

  Note the split points are only trusted when something *references* them.
  Linear disassembly desynchronises on data in `.text` and then invents `ret`s:
  of the first five unreferenced candidates checked by hand, two disassembled to
  garbage. So the figure is a lower bound and is meant to be — 260 candidates,
  186 confirmed. Do not rewrite `functions.tsv` from the naive scan.
