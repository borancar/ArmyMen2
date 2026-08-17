# Working on this repo

Standing conventions and decisions for the Army Men II reconstruction. Findings
live in `docs/` — `00-recon.md` for what the binary is, `01-harness.md` for how
we drive it. This file is the part that is *policy* rather than discovery: the
things that would otherwise have to be re-litigated, or re-learned the hard way,
on every new machine and in every new session.

## What this project is

Army Men II (3DO, 1999, Win32, PE32 i386) ported to Linux by **reconstructing
the source piecewise**. The original `ArmyMen2.exe` keeps running under Wine and
its functions are replaced one at a time with reimplemented ones, each verified
against real gameplay before moving on. Not a big-bang rewrite.

Relocations are stripped, so the image always maps at `0x400000` and hardcoded
VAs are stable. There is no PDB and there cannot be one: the Debug Directory is
empty (entry 6 = 0/0), with no CodeView record.

## Non-negotiables

**Always `WINEPREFIX="$PWD/.wine"`.** The prefix lives in-tree.

**Fixed-width types everywhere.** `<stdint.h>` — `uint8_t`, `int32_t`,
`uint32_t`, … — never bare `int`, `long`, `short`, `unsigned`, and never Win32
typedefs like `DWORD`/`WORD`/`BYTE` in reconstructed code. This is a deliberate
deviation from what the 1999 MSVC 6 source looked like. The original is 32-bit
x86 where `int` and `long` are both 32 bits; the port targets 64-bit Linux where
`long` widens and would silently change struct layouts and arithmetic.

**The leaked Army Men 1 (1998) source is names and structs only.** Use it for
identifier naming and struct-layout hints; derive *all* logic from disassembly
of `ArmyMen2.exe`. It is a strong reference — 8 of the 10 source filenames
recovered from the AM2 binary also exist in the AM1 tree — but it is a leak, and
leaning on it for logic would make this a derivative work and foreclose
publishing it cleanly. Never copy it into the repo; consulted material stays in
the git-ignored `reference/`. Never quote it into commits, docs, or sources.

**Do not restate Windows or DirectDraw structures.** Use the real SDK headers.
Everything Win32 goes through `src/inject/win32.h`, which is the single place
that sets `CINTERFACE`/`COBJMACROS`, pulls in `windows.h` and `ddraw.h`, and
undoes the `winuser.h` `DrawText` macro collision.

**Exactly one launch target: `make run`.** No `play`, `run-log`, `run-debug`.
Variations are overridable make variables — `TRACE`, `GAMELOG`, `OBSERVE`,
`ARGS`, `DESKTOP`, `ID`, `ISOLATE`. The one permitted sibling is `run-stock`,
which launches the unpatched GOG binary as an A/B reference; that is a genuinely
different thing, not a variation. Near-duplicate targets drift apart and it
stops being obvious which is canonical. When a new option appears, add a
variable to the existing recipe and document it in the comment block above it.

Runs are independent so several can go at once: desktop name, control port, log
file and screenshot directory are all derived from `ID`, which defaults from
`$DISPLAY`.

## Language split

`src/game/` is **C++** (`.cpp` sources, `.h` headers). `src/inject/` is **C** —
it is harness, not game.

The reason is the ABI survey (`tools/checkabi.py`): of the 1,239 game functions
below the CRT, **100 are thiscall**, which on i386 means non-static member
functions. Reconstructing those in C would mean hand-written
`__attribute__((thiscall))` shims around what the original wrote as ordinary
methods.

Consequences to remember:

- Every `src/inject/*.h` that declares a function shared with `src/game/` needs
  an `extern "C"` guard. `win32.h`, `orig.h` and `sites.h` declare none and must
  be left alone — wrapping `win32.h` in particular would be wrong, since it
  pulls in `windows.h`.
- C++ will not implicitly convert a function pointer to `const void *`, which is
  what `patch_replace` takes, so install sites cast explicitly.

## Verifying a reconstruction

Build, install, run, drive, screenshot, check counts:

```
AM2_DISPLAY=:99 AM2_MAKEVARS="TRACE=1" tools/drive.sh start
tools/point.py X Y --click        # absolute positioning
tools/drive.sh ctl counts         # per-function call counts
tools/drive.sh shot NAME
tools/drive.sh stop
```

Boot Camp reaches gameplay quickly and exercises the map, sprites, HUD text and
the lock/unlock bracket. The title screen alone touches almost none of the
engine, so it proves very little. From the briefing screen, **`RETURN` starts
the mission** — the cursor is hidden there, so `tools/point.py` cannot find it
and clicking is not an option.

**Quitting through the menu exercises code that killing the process cannot.**
`tools/drive.sh stop` kills the tree, so the whole shutdown path never runs.
Click QUIT on the title screen, then OK on the CONFIRM GAME EXIT dialog
(roughly `306,383` then `475,224`), and the game leaves the way it was meant
to — which is the only way to reach `CommShutdown` and the comm teardown.

**The campaign is a third gameplay path, and reaching it needs typing.** SINGLE
PLAYER → RECRUIT → type a name → OK drops straight into MAP 01, a different map
from Boot Camp with a different object count.

**Typing is `drive.sh ctl "type <text>"`, and no X cooperation is needed.** A
text field reads `WM_CHAR`, which the socket's `key` command cannot produce —
that injects DirectInput, which is what the game polls for menus and movement.
Posting the messages to the window does, entirely inside the process. Two of
this game's behaviours decide how, and both fail silently if ignored:

- `WndProc` drops a `WM_CHAR` whose predecessor was also a `WM_CHAR`, because a
  real keystroke is a keydown then a char, so two running can only be a
  duplicate. Post a bare string and only its first character arrives.
- `PumpMessage` calls `TranslateMessage`, so the `WM_KEYDOWN` produces a second
  `WM_CHAR` by itself — lower case, since no shift is held. Send keydown and
  char together and everything doubles: `Bbiigg Bbaattttllee`.

The working shape is the full `WM_KEYDOWN`, `WM_CHAR`, `WM_KEYUP` a keyboard
sends, with a pause before the keyup so the pump dispatches our char and the
translated copy back to back and the game's own duplicate check eats the
second. See `src/inject/control.c`. This is also the only way `WM_CHAR` in
`winproc.cpp` gets exercised.

**`ARGS=-dbg` — dropping the default `-nointro` — is a second configuration
worth having.** The Smacker intro is a code path of its own: it is the only
caller of `SnapshotSystemPalette`, and the movie coming out in the right
colours is a direct check on the GDI palette code that nothing else exercises.
Between this, `-w`, and plain Boot Camp there are three distinct startup paths.

**The multiplayer path is a fourth configuration, and it needs
`AM2_MULTIPLAYER=1`.** Without it the title screen has no MULTI-PLAYER entry —
that button was patched out of this build, see `docs/binarypatches.md` — and
the entire DirectPlay subsystem is unreachable, so every reconstructed comm
function is verifiable only by reading. With it:

```
AM2_DISPLAY=:99 AM2_MAKEVARS="TRACE=1" tools/drive.sh start 25 AM2_MULTIPLAYER=1
tools/point.py 306 222 --click     # MULTI-PLAYER
tools/point.py 200 176 --click     # the TCP/IP row
tools/point.py 515 221 --click     # SELECT
tools/point.py 321 262 --click     # JOIN A WAR  (321,222 is START A WAR)
```

That reaches COMM. CHANNEL SELECT, then START A WAR / JOIN A WAR, then CHOOSE
A BATTLE with an empty session list. It exercises `CommCreateDirectPlay`,
`CommEnumConnections`, `CommClose`, `StartSelectedGame`,
`StartMultiplayerGame` and `CommEnumSessions` — the last polled repeatedly
while the browser is open, so its count climbs on its own.

Two readings not to misinterpret. Choosing the last row, "Play Against
Computer Only", takes `StartSelectedGame`'s local branch and ends at ENTER
BATTLE NAME. And `CommInitializeConnection` and `FindGameCD` stay at 0
throughout, because the reconstructed callers reach them directly rather than
through the patched entry — the usual blind spot, not a failure.

**`AM2_NOPATCH=1` is the A/B, and `run-stock` is not.** It installs the harness
— logger, input hook, control socket — and none of the reconstruction, so the
same scripted run can be played on the original code and on ours and the
results compared. `run-stock` drops the harness too, which means it cannot be
driven, cannot be logged, and does not in fact start on this machine.

Done on all three configurations, and the results are worth quoting:

| run | game's own log | pixels differing |
|---|---|---|
| Boot Camp, fullscreen | identical, 14 messages | 22 / 786,432 |
| intro, `ARGS=-dbg` | identical, 5 messages | 81,494 — the film is playing |
| windowed, `-w` | identical, 6 messages | **0** |
| audio, silent ALSA device | identical, 13 messages | 22 / 786,432 |

`audio` is the same run as Boot Camp with a sound device attached, and it is
not redundant: without one, DirectSound never starts and nine reconstructed
functions never execute, so `bootcamp` compares them not at all. With it,
`WaveOpenFile`, `WaveStartDataRead`, `WaveReadFile` (498 calls), `RefillAudioBuffer`,
`StartAudioStream`, `StopAudioStream`, `SetStreamVolume`, `InitDirectSound` and
`InitWaveSounds` all run and all match. Its log is one message shorter because
the wave-loading failures are gone.

The message counts drift by one or two between builds as reconstructed code
takes over lines the original logged; what matters is that the two sides of a
run agree, not the absolute number. The intro's pixel figure is meaningless by
construction — two unsynchronised playbacks of the same movie — so its log is
the only evidence it carries.

The windowed frame is static, and it comes out *pixel-perfect* — which also
settles the Boot Camp figure, since a scene with nothing moving gives exactly
zero and one with animation gives 22 scattered pixels. Windowed also reproduces
the screen rectangle byte for byte, `04000000 1e000000 84020000 fe010000`,
so `PositionWindow`'s windowed branch is confirmed numerically and not by eye.

`tools/ab.sh bootcamp|windowed|intro|audio|all` runs the whole thing, and each
configuration now has a pixel budget it must stay inside — 0 for windowed,
which is static, 500 for the two Boot Camp runs, and none for the intro, which
is two unsynchronised playbacks of a film. Exceeding it fails the run.

That is there because it once did not. A reconstruction of the map tile
painter misdecoded its rows, drew 33,137 wrong pixels, and `ab.sh` reported
**A/B clean** — it only failed on the log. The number was printed on the line
above and nothing acted on it. `AM2_AB_PIXELS` overrides the budget, mainly so
the check itself can be tested. Repeat it after
any large batch of reconstruction; it is far stronger than the invariant, which
only checks one subsystem.

Two traps it now guards against, both of which bit while it was being written.
`make config` must be called with `DISPLAY` set or it names the log for ID 0,
which this run never wrote — and two missing files diff as identical, so it
reported a clean A/B on no data at all. And the game's loading-progress `]`
lines end in CR, so an `^\]$` filter silently misses them; their count varies
with load time, so comparing them fails at random. An A/B that can pass on
nothing, or fail on noise, is worse than not running one. Note the counts are empty under `AM2_NOPATCH` — the counters *are*
the trace stubs, installed by `patch_replace` — so the evidence is the log and
the pixels, not the counters.

**Launch through `tools/drive.sh`, never a bare backgrounded `make run`.** A
`setsid make -s run ... &` issued from a script or an agent shell starts the
game, gets as far as `system speed:` in the log, then fails inside `InitInput`
and exits — in every configuration, including ones that work perfectly through
`drive.sh`. The mechanism is not understood. What matters is that it fails
*silently and plausibly*, deep in DirectInput setup, so it reads exactly like a
broken reconstruction. An afternoon was lost to this: it produced a completely
convincing false result that windowed mode was broken, which survived several
rounds of A/B against `run-stock` because `run-stock` was being launched the
same way and failing for the same reason. If a run fails in a way that seems to
implicate recent work, re-run it through `drive.sh` before believing it.

To pass switches through, use `drive.sh start`'s trailing `VAR=VAL` arguments,
which reach `make` as single words. `AM2_MAKEVARS` is word-split, so a value
containing spaces breaks apart — and `AM2_MAKEVARS="ARGS='-nointro -w'"` hands
make a bare `-w`, which it takes as `--print-directory`:

```
AM2_DISPLAY=:99 tools/drive.sh start 25 "ARGS=-nointro -dbg -w"
```

**A count of 0 does not mean "broken" and does not mean "never called".** When a
reconstructed function's callers are *also* reconstructed, the direct call
bypasses the patched entry point and the counter never moves. The two cases are
indistinguishable from the outside; resolve it with a temporary probe rather
than by guessing. `BlitCopy16`, `BlitCopy32` and `EncodeGlyph` all read 0 for
exactly this reason, and so now do `InitApplication`, `PumpMessage` and
`PositionWindow` — reconstructing `WinMain` swallowed the whole layer below it
in one go. `AM2_PROBE_NOWIN=1` is the probe for that particular blind spot: it
leaves the four application-layer functions original and every other patch in
place, which is something `run-stock` cannot do.

Note that this makes the counts *less* informative the further the
reconstruction gets. It is a measure of what still crosses an original
boundary, not of what runs.

**The registry invariant is the sharpest single check available — on Boot
Camp.** `FirstItem` walks × objects registered == `NextItem` calls, exactly —
e.g. 91,173 × 1,609 == 146,697,357.

It is narrower than it looks, and the scope matters. The identity holds only
while the object count is constant across every walk, which is true of Boot
Camp because the whole map is registered during load, before anything walks.
On campaign MAP 01 it does not hold: 1,951 walks, 618,491 `NextItem`, 325
registered, 0 removed — and 1,951 × 325 is 634,075, not 618,491. The game was
demonstrably fine, rendering the map and HUD correctly. 618,491 / 1,951 is
317.01, i.e. slightly *fewer* objects per walk than the final total, which is
what "some objects were registered after walking began" predicts and is not
what a broken reconstruction would look like.

So: an exact match on Boot Camp is strong evidence. A mismatch anywhere else is
not evidence of a fault on its own — check whether registration overlapped the
walks first. (The obvious confirmation on MAP 01, watching the ratio
converge on 325 as more walks happen at the final count, is still not done.)

**The Boot Camp dialogs do dismiss, and getting past them is worth doing.** The
mission opens with MESSAGE FROM HQ over the map — its OK is at roughly
`476,224` — and behind that is a full-screen instruction sign that any click
clears. Past both, the mission is properly live: Sarge on the map, the HUD
drawn, the frame ticking. That is where the interesting counts appear.
`Update3DAudioVolumes` goes from 121 to 9,623 simply by getting the dialogs out
of the way.

It is also where Boot Camp's invariant can be read cleanly: one run gave
`FirstItem` 519 and `NextItem` 835,071, and 519 × 1,609 is 835,071 exactly.

`ESCAPE` does nothing there — there is no in-game menu behind it — so this is
not a route to the shutdown path.

**Compare `ret N` explicitly before assuming a shared signature.** A diff that
normalises jump targets hides the epilogue. Getting this wrong is what made
`BlitCopy16` crash: the copy variants are `ret 0x14` (5 args), not `ret 0x18`
(6), and an extra parameter made GCC over-pop four bytes.

## Driving input

The control socket speaks **relative** deltas, because that is what the game
reads — it takes buffered DirectInput `GetDeviceData`, not `GetDeviceState`.
Wine's mouse acceleration is non-linear on top of that (~1.75× for a 100-pixel
step, ~2.0× for a 300-pixel one), so a single computed delta overshoots. Use
`tools/point.py`, which closes the loop on a screenshot instead of modelling the
curve. Its cursor threshold was sampled from a frame with the cursor pinned to
the corner, not guessed: a loose one also matches title-screen dirt at
(181,156,88) and will cheerfully report rubble as the pointer.

## Build and install hazards

Header dependencies are tracked with `-MMD -MP`. Do not remove this. The build
originally compiled every source in one command, so header edits were always
picked up; splitting into per-object rules silently lost that, and the symptom
is edits that appear to do nothing.

`install-hook` copies to a temp name and `mv`s into place. **Never overwrite a
mapped DLL** — a plain `cp` corrupts a running instance.

When killing the game, bracket the pattern: `pkill -f 'ArmyMen2[.]exe'`. Without
the brackets the pattern matches the killing shell itself. A surviving game also
keeps holding `ArmyMenMutex`, which silently makes the next run in that prefix
exit; `tools/drive.sh stop` walks the process tree for this reason.

## Open items

- Lock/Unlock bracket batch: 5 of 22 done (`DrawText`, `DrawSprite`,
  `RenderGlyph`, `RedrawMapRegion`, `CalibratePalette`). Next bottom-up:
  `0x00454F00` (144B), `0x00414620` (224B, tooltip renderer), `0x00413610`
  (256B), `0x00433350` (304B).
- **A vtable call is only COM if `this` is pushed.** Under `CINTERFACE` every
  COM method takes the interface as an explicit first argument, so it goes on
  the stack; an i386 MSVC C++ virtual is thiscall and keeps `this` in `ecx`.
  The two compile to the same `mov vt,[obj]` / `call [vt+N]` pair, so a survey
  that matches only on shape reports the engine's own destructor chains as
  DirectX — and the densest-looking candidates, 48 bytes of nothing but vtable
  calls, are exactly those. `push 1` into slot 0 is the clearest tell: that is
  the MSVC scalar deleting destructor, and COM's slot 0 is `QueryInterface`,
  which takes three arguments. `tools/comcalls.py` records this as its `abi`
  column; 90 of 353 in-game sites are C++ rather than COM.
- The Win32/DirectX boundary is inventoried and being worked outward-in: 122
  functions below the CRT touch the import table (`docs/imports.tsv`) and 110
  contain genuine COM dispatch (`docs/comcalls.tsv`). Done so far: `WinMain`,
  `InitApplication`, `PumpMessage`, `PositionWindow`, `WndProc`,
  `InitDirectDraw`, `InitInput`, `CreateOffscreenSurface`, `ClearSurface`,
  `RealizeSystemPalette`, `SnapshotSystemPalette`, `ReportError`, `FatalError`,
  the three `Wave*` helpers, both DirectPlay creators, the two bitmap loaders
  (`CreateBitmapSurface`, `ReloadBitmapSurface`) and the comm object's
  constructor and destructor. The window, the message queue, the display mode,
  every surface, both input devices, the GDI palette, all `.WAV` reading,
  sprite upload from a stream, the whole network transport and the entire
  registry surface are ours.

  Do not read the leftover as work outstanding. Of the 87 import-touching
  functions not reconstructed, `tools/coverage.py` classes 83 as game logic —
  a `GetTickCount` or a `PostMessageA` inside something that is otherwise not
  boundary at all — leaving 4 functions and 13 sites: `0x0042F170`,
  `0x0042ECE0`, `0x0044D110` and `0x0040ED10`, every one of them a
  `MessageBoxA` and its `GetActiveWindow` inside menu code. The channels
  themselves are owned: every DirectX object in the process is created,
  configured and destroyed by reconstructed code, and the registry is opened
  and closed by ours. What still dispatches through COM is game logic holding a
  handle it did not make.
- **A decline is worth revisiting when the reason was uncertainty rather than
  scope.** `0x0040BCF0` sat on the list below for most of a session because its
  position fields looked like they aliased — `[eax+0x12]` on one path and
  `[eax+0x10]` on another, apparently two overlapping fields of one record. They
  are not the same record: a lookup call between them reassigns `eax`, so one is
  the game object's position and the other the sound's, and both are plain
  `AM2_Point`. Reading it a second time took minutes. "I could not follow this"
  ages differently from "this is game logic"; only the second is a decision.
- **Read and deliberately left original.** These come back to the top of every
  candidate ranking, so they are listed here rather than re-read each time. All
  are game logic that happens to hold a device handle, and the standing brief
  says to use original functions for pure game and menu logic.

  | | why |
  |---|---|
  | `0x0040CED0` 1792B, 12 | the sound engine proper |
  | `0x00427070` 944B, 5 | input-to-command translation |
  | `0x0041BE80` 832B, 8 | sprite cache management |
  | `0x00412FE0` 1184B, 4 | menu logic |
  | `0x00425AF0` 288B, 5 | map object placement |
  | `0x0041B0E0`, `0x0041D060`, `0x0042D9B0`, `0x0042DA30`, `0x0042F170`, `0x0042FF60` | all ≥120 B per COM call, all game logic |
  | `0x00453BC0` 48B | not COM at all — a C++ destructor chain, per the `abi` note above |

- **The import side is done, in the only sense the word can bear here.** Every
  Win32 call site in the image that can actually execute is now either inside
  reconstructed code or incidental — a `GetTickCount`, an `IntersectRect`, a
  mutex wait. What is left outside is three `MessageBoxA` calls, and all three
  sit behind copy-protection checks that have been patched to skip them, so
  none can run. `tools/coverage.py` reports the symbols and
  `docs/binarypatches.md` explains why they cannot fire.

  This claim is about the IAT only. DirectX reached through COM is a separate
  count and is *not* finished: 10 functions with 30 calls on objects
  `tools/comcalls.py` can name, plus an unmeasured share of the 182 sites whose
  object it cannot. Do not read "the boundary is done" off this bullet alone.
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
  with being correct — which is why `src/game/cdcheck.h` records the retail
  check behind an `#ifdef` that is off.
- **The game has no networking imports at all** — no ws2_32, no wsock32, no
  dplayx, and not even those strings in `.text`. Its multiplayer transport is
  DirectPlay reached through COM, so the only trace in the import table is
  ole32's `CoCreateInstance`, twice. Both are now `src/game/dplay.cpp`. Worth
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
  drift from what the harness installs. Currently 125 of the 140 genuine
  boundary sites are ours; the 5 functions
  and 16 sites left are four game-logic routines with a MessageBoxA error path
  and the comm constructor's registry read; the other 136 sites in the binary are game logic
  that happens to read a clock.
- **`obj -> table -> slot` with no `this` is a real shape in this binary, and
  it needs two dereferences.** `0x0065A058` (the repaint object) and
  `0x006568A0` (the current movie) are both reached as
  `mov ecx,[global]; mov eax,[ecx]; call [eax]`. Writing that as one
  dereference calls the vtable pointer as if it were a function, and the game
  exits instantly with nothing useful in the log. Cost an iteration; use a
  named local for the object and another for the table rather than a nested
  cast.
- **Pick the next target by boundary density, not by import count.** Ranking
  what is left by sites-per-byte finds functions that are boundary code;
  ranking by sites alone finds 5,760-byte game-logic functions whose only
  contact with Win32 is a `GetTickCount`. Reconstructing one of those to
  capture a timer read is exactly what this port is not for. Everything under
  ~50 bytes per import site is worth looking at; above that, read it first.
- **Name a function from its body, not from one call site.** `0x0041AD30` went
  in as `AttachPalette` because that is what it looked like where
  `InitDirectDraw` calls it. It is a colour fill — vtable slot 5 is `Blt` — and
  the wrong name survived a commit. Reading the callee costs a minute;
  a wrong name in `orig.h` propagates into every module that picks it up.
- **A reconstruction can break the harness rather than the game.**
  `src/inject/dinput_hook.c` works by patching the game's IAT slot for
  `DirectInputCreateA`. A reconstructed `InitInput` that imported the symbol
  into `am2hook.dll` would resolve through *our* IAT, walk straight past the
  hook and silently disable all injected input — the game would still run and
  look perfectly healthy. `src/game/device.cpp` calls the game's own import
  thunks (`0x00463396`, `0x00464410`) instead, which read the patched slot at
  call time. Check for a harness hook before reconstructing anything that calls
  an import.
- **Not every reconstruction has to be a patch.** `WndProc` is registered, not
  detoured: the only reference to `0x0040A6B0` in the whole image is the
  `WNDCLASS` field in `InitApplication`, and that is ours now. Because the
  original is left intact it stays *callable*, so the six comm messages that
  are pure game logic are forwarded to it instead of being reconstructed. Look
  for this shape before detouring anything reached through a function pointer —
  a callback, a vtable, a dispatch table. It buys back the thing detouring
  costs, which is the ability to defer.
- **`-w` is windowed mode**, global `0x00507344`, and it gates far more than it
  looks: the window border and repositioning, the palettized primary in
  `InitDirectDraw`, and `CalibratePalette`. Anything that reads 0 under the
  default fullscreen run may simply be behind it. The other switches are in
  `orig.h`; three are developer names, and `-rob` is the flag that was already
  known as `ADDR_DEBUG_ITEMLIST`.
- Windowed mode runs and is worth using as a second configuration — the window
  is created, sized and positioned correctly (client area 640x480 at (4,30))
  and `CalibratePalette` fires — but Wine hands back no lockable primary, so
  `LockSurface` never succeeds and the client area stays black. Fullscreen
  remains the configuration to verify against.
- Both DirectDraw `Restore` paths are untested. `LockSurface`'s is a real defect
  in the original — it publishes an uninitialised descriptor after a successful
  Restore without re-locking. Kept as-is deliberately; see `src/game/surface.cpp`.
- Unexercised so far: `RemoveFromItemList`, `KeyFieldC`, `CheckSaveTag`,
  `WaveCloseReadFile`, and `RefreshScreen` — that last has 7 callers and is
  reached by none of Boot Camp, the intro, the HQ dialog or F1, so whatever
  forces an out-of-band repaint is somewhere further in. `CalibratePalette` came off this list once
  `-w` was understood — it runs twice per windowed startup, and
  `SnapshotSystemPalette` came off it once the intro movie was allowed to play.
- **Audio can be exercised without a sound device, and must be.** There is no
  PipeWire or PulseAudio session here, so DirectSound will not start and every
  audio function returns at its first line. That left the largest block of
  reconstruction in the tree verified by reading alone — and the build, the
  fingerprints and the A/B on all three configurations all pass whether that
  code is right or wrong.

  ALSA's `null` plugin is built into libasound and needs no server at all:

  ```
  export ALSA_CONFIG_PATH=$PWD/tools/alsa/asoundrc AM2_DUMP_SOUND=1
  AM2_DISPLAY=:99 tools/drive.sh start 25 "ARGS=-nointro -dbg"
  tools/checkwaves.py
  ```

  DirectSound then starts, all 56 waves load, and `checkwaves.py` confirms the
  bytes handed to each buffer are byte-for-byte the `.WAV`'s `data` chunk —
  which exercises `WaveOpenFile`, `WaveReadFile`, `LoadWaveSound` and
  everything under them.

  That config is deliberately self-contained rather than including
  `/usr/share/alsa/alsa.conf`: that file pre-loads `alsa.conf.d`, where
  `99-pipewire-default` re-points `default` at PipeWire through a hook that
  runs after anything a later override says. The only symptom is "Host is
  down".

  It earned its keep immediately: `LoadWaveSound` was leaving the
  `DSBUFFERDESC` with no format and no length, so every `CreateSoundBuffer` in
  the game failed. The reader writes both fields straight into that structure,
  which is why the original only assigns `dwSize` and `dwFlags` by hand.

  A mission, not just the title screen, is what exercises the rest. Clicking
  BOOT CAMP and then pressing `RETURN` at the briefing gives, over one run:

  | function | calls |
  |---|---:|
  | `WaveReadFile` | 498 |
  | `Update3DAudioVolumes` | 121 |
  | `PlaySoundAt` | 35 |
  | `WaveOpenFile`, `StartAudioStream`, `SetStreamVolume`, `RefillAudioBuffer` | 2 each |
  | `InitDirectSound`, `InitWaveSounds`, `FillSoundBuffer` | 1 each |

  `tools/ab.sh audio` drives that same sequence, so all of it is compared
  against the original and not merely run.

  Still at zero, and all of them teardown: `StopNamedSound`,
  `FreeDynamicSounds`, `StopAllSounds`, `FreeSound`, `ReleaseSoundBuffers`,
  `WaveCloseReadFile`. Leaving a mission cleanly is the obvious way to reach
  them and has not been tried.
- **`CommOnConnected` (`0x0040E660`) cannot run, and the reason generalises.**
  Its only reference is inside `CommCreateDirectPlay`'s `if (connection)`
  branch, and that function's single caller at `0x0042EE78` passes a literal
  `0`. So the branch is dead and so is everything behind it — including the
  `InitializeConnection` in the same branch. The transport is actually brought
  up by `CommInitializeConnection` from `StartSelectedGame`.

  Worth checking for before spending time trying to exercise something: a
  function can be reachable, called from live code, and still never run because
  the argument that gates it is a constant at the one call site.
- Object types 2, 3 and 8 are still unidentified.
- `object.aai` complains about `link 33-1..4`; unexplained.
