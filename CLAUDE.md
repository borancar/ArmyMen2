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

**`make check` runs everything that does not need the game.** Eight analysis
tools plus a drift check that fails if any generated file under `docs/` no
longer matches what the tools produce.

One of the eight is `tools/checkclaims.py`, which reads the numeric claims out
of *this file* and recomputes them. It exists because three separate figures
here were found stale by measuring rather than reading, each from the same
cause: a tool changed, some prose was updated, the rest kept asserting the old
number. It is deliberately short — most of this file is judgement and cannot be
checked, which is the argument for keeping numbers in `docs/boundary.md` and
pointing at them from here. Seconds, no display, and it is the half
of verification `tools/ab.sh all` is not.

It catches a tool whose output changed without being regenerated — tested by
making `coverage.py` print a different heading, which fails the target. It does
NOT catch a hand-edit to a generated file, because the tools rewrite those
before git is consulted; the edit is healed silently rather than reported.

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

## Source layout

`src/game/win32/` holds every module that talks to Win32 or DirectX; the flat
part of `src/game/` holds the reconstruction that touches no API at all —
`blit`, `dist`, `objtable`, `objtype`, `packkey`, `rect`, `savetag`, `text`.
Those eight are software rasterisers, rectangle and distance maths, the object
tables, key packing and save tags: pure computation over memory the caller
supplies. Everything else — 14 modules — is the boundary, and the split is the
answer to "what still talks to the outside world" in directory form.

The test for which side a file belongs on is whether it names a Win32 or COM
type at all. `blit.cpp` mentions `IDirectDrawSurface` once, in a comment
explaining where the original's fallback came from, and stays flat; it operates
on a locked pointer somebody else obtained.

Includes are written out in full rather than resolved by `-I` flags, so a
module's directory is visible at its use sites: `win32/` sources reach the
harness as `"../../inject/orig.h"` and the flat half as `"../blit.h"`.

**Four tools derive "what is reconstructed" by scanning these sources**, and
all four used a non-recursive `listdir` before the split. Adding a
subdirectory would have made every one of them miss fourteen modules silently
and report the boundary as barely started. They now share `am2.game_sources()`
— one definition, for the same reason `tools/merges.py` imports
`coverage.REGISTERED` rather than copying it.

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

## Differential testing in the game's own process

**`AM2_SELFCHECK=1` calls each reconstruction and the original it replaces,
side by side, inside the running game.** The harness is already injected, so
the original is at its address in the same address space: no emulator, and more
to the point no translation. One set of pointers, one set of globals, a scratch
buffer both sides address identically.

It must run BEFORE `install()` patches anything. A patch overwrites the
original's first five bytes with a jump and there is no trampoline, so after
that the original is not callable at all — that one moment is the whole
opportunity.

5,504 calls across 43 functions, and swapping `min` for `max` in `ApproxDistXY`
makes 124 of them disagree with the argument printed.

**It cannot pass NULL, and that is the one thing the offline harness does
better.** A null argument that faults simply drops a vector under Unicorn; here
it takes the game down, and `ApproxDist` — which dereferences unconditionally —
killed the process on the second function tested. Null paths stay the emulator's
job.

**Everything the offline harness needed fixing for was a consequence of having
two address spaces**: a NULL argument emitted as `0 - SCRATCH`, written pointers
that could not be compared byte for byte, a replay buffer smaller than the
emulator's map, seeded pointer chains that had to be rebased on arrival. None of
those exist in-process. The emulator is still worth having because it runs in
seconds with no game at all, which is what makes it usable while writing a
function — but it was the wrong default and the injection was there all along.

## Differential testing without the game

**It has already caught a misreading that would have shipped.** `AngleDelta`
was written from a disassembly that stopped at the function's first `ret`,
which hid a second branch, and went in with a confident comment explaining an
asymmetry the function does not have. The vectors failed on the first run:
`AngleDelta(255, 2)` is 3, not -253. A function with two returns is ordinary,
and any helper that stops at the first one will misread it every time -- check
the whole body, or let the vectors tell you.

**`make selftest` checks a reconstruction against the original binary with no
part of the game running.** `tools/vectors.py` emulates the ORIGINAL function
with Unicorn over the mapped PE image, records (inputs → output) vectors into
`tests/vectors.h`, and `tests/selftest.cpp` replays them against our C++. A
failure names one function and the arguments that expose it, which the
whole-game A/B has never been able to do.

It only works for functions that read no global data — one that reads a global
needs that global mapped, and mapping it means starting the game. **161 of the
433 unreconstructed leaves qualify**, and 99 of those already yield vectors.

Validated on the 17 pure functions that were already reconstructed: 533 vectors,
all passing. Tested in the failing direction too, and the two results are the
point of it — replacing `lo >> 1` with `lo / 2` in `ApproxDist` still passes,
because for non-negative operands they are the same function, while swapping
`min` for `max` fails 21 vectors with the arguments printed.

**angr supplies inputs, Unicorn supplies expected outputs, and the split
matters.** Random arguments are weak at branch coverage: the min/max swap was
caught by only 13 of 512 random vectors. angr solves for one input per path
instead. It never gets a vote on what the original does — that always comes
from the Unicorn run, so there is one source of truth.

**A reconstruction that reads a constant table in the image is testable
offline too, and the header that said otherwise was half wrong.**
`tests/selftest.cpp` used to state that a function reading a global "would need
that global mapped, and mapping it means starting the game". Only the first
half is true. `tests/loadimage.h` copies the image's sections in from the file
with nothing executed and no game running.

It cannot put them at `0x00400000`, and no link-time base fixes that: Wine's
loader maps `locale.nls` across `0x00380000..0x00443000`, then `c_1252`,
`c_437` and `sortdefault` through `0x0084A000`, before any user code runs. So
the image lands wherever `VirtualAlloc` puts it and `src/game/image.h` carries
the difference -- an image address is written `AM2_IMAGE(0x00487C90)` and
resolves through a slide that is zero in the game. One add. The native ELF,
where `0x00400000` is equally unavailable, will need exactly this and nothing
more. What stays out of reach is a global the game WRITES at runtime, and a
call into the image; those still need `AM2_SELFCHECK=1`.

**Where the program ships its own input, use that instead of vectors.**
A keyword lookup learns nothing from a random 32-bit argument.
`tools/scriptcheck.py` runs the ORIGINAL tokeniser under Unicorn over every
distinct word and every distinct line in the 109 shipped `.txt` files --
15,228 words and 13,956 lines, 72,209 tokens -- and `selftest` replays the lot.
`AddToken` is hooked rather than executed, because it reaches `HeapAlloc`,
which does not exist under emulation, and running it would only rebuild a list
the hook already has.

It caught a misreading on its first run. `ParseNumber`'s loop bound is
`i < len`, not `i < len - 1`: the `repne scasb` that measures the string counts
the terminator, so `not ecx` gives len+1 and the following `dec` gives len.
With the off-by-one `"1."` parsed as the integer 1 where the original gives the
float 1.0. Nothing in a mission file ends a number with a dot -- what exposed
it was the numbered headings in the EULA that ships beside the scripts, which
the corpus includes because it takes every `.txt` under the prefix and not only
the ones the game loads. **Take the whole corpus, including the parts that are
not input.**

**Say which mutations the corpus does NOT catch.** Making `/` not end a line
fails on every `// comment`, and stopping `>` from pairing splits `<>` in the
mission conditionals -- but moving the word clamp from `0x3F` to `0x40` passes
all 13,956 lines, because no token the game ships is 63 characters long. That
path stays verified by reading, and the test's own comment says so. A test
whose gaps are unstated reads as more coverage than it has.

**An emulated heap makes whole statement handlers testable.** `AddToken` and
`AddNameTableName` reach the game's `malloc`, which reaches `HeapAlloc`, which
does not exist under emulation -- so they had to be hooked away, and anything
built on them could not be run at all. A bump allocator in a region of its own
lets both run for real, and `tools/scriptcheck.py` now emulates `variable` end
to end: tokenise a line, run the handler, read back the name table. Nothing is
reclaimed on purpose; `free` becoming a no-op cannot change what a correct
caller observes, and a real allocator would be a second thing to be wrong
about.

**A handler's value is in the exits a shipped script never takes.**
`ScriptVariable` has four and the scripts reach one. Declaring with the wrong
type and failing to rewrite the name token both fail the corpus -- but deleting
the duplicate-name check passed all 196 cases, because every case started from
an empty table. Two cases now run a prior declaration first. Ask what state a
check needs before believing a corpus covers it.

**`Emu.call`'s instruction cap is a runaway-loop guard, not a budget.** It was
hardcoded at 100,000, sized for the pure leaves it was written for, and the
tokeniser exceeds it honestly: `LookupToken` walks all 185 keywords for every
word and `ParseNumber` re-runs `strlen` per character, so a 213-character line
of prose reached it and was discarded as a fault. It is a parameter now. Raise
it before concluding a function faulted.

**Coverage is measured, not claimed.** A Unicorn code hook records every
instruction a vector set reaches, and `tools/vectors.py` prints the percentage
per function; trailing `nop`/`int3` padding is excluded, since the linker's
alignment is not reachable and counting it would put 100% out of reach for
everything. All twelve pure functions in the validation set reach **100%**.

Getting there needed one thing that is obvious in hindsight: **NULL belongs in
the pointer candidate set.** Almost every accessor in this binary opens with
`test eax,eax; jne; ret`, and generating only valid pointers left that early
return unreached — which is exactly the instruction a reconstruction is most
likely to forget. `ObjIsItem`, `ObjIsType2` and `ObjIsType3` sat at 90-92% for
that one reason.

**A pointer argument's variation is in the memory it points AT.** The first
version left that concrete and found 8 "paths" through `ApproxDist` that all
had identical inputs, because both its arguments are pointers and there was
nothing symbolic left to solve. 16 bytes behind each pointer are symbolic now,
and the bytes angr chooses travel with the vector.

**The purity test must cover the whole data range.** A first version matched
only addresses beginning `0x4`, which silently skipped everything at `0x5xxxxx`
and `0x6xxxxx` — and `.data` runs to `0x667000`. It reported **315** pure
functions where there are **161**, because `NextItem` reading `[0x514F08]`
looked like a pure function of its arguments.

## The script interpreter

The game ships its missions as readable text -- `data/<map>/<map>N.txt` and
`rules/*.txt`, 109 files under the prefix -- so this is the one subsystem whose
names come from the program's own vocabulary rather than from us. The chain is
`WinMain -> RunFrame -> ADDR_STATE2_FRAME -> LoadLevelScript (0x00425060) ->
ReadScript (0x00444CD0) -> ParseLine (0x00444C40) -> NextToken (0x0043F450)`.

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

**`tools/ab.sh quit` covers the teardown, and it found a real bug the first
time it ran.** Until it existed, `ShutdownDirectDraw`, `ShutdownInput`,
`ReleaseSoundBuffers`, `FreeSound`, `FreeDynamicSounds` and the sprite frees
had never executed once — every configuration killed the process instead. A
clean exit runs all of them, and `trace_report()` on `DLL_PROCESS_DETACH` is
the only way their counts are ever visible.

It is also the one configuration where line ORDER is not deterministic. The
packet thread and the receive thread each log a line as they finish, and those
two swap places between runs — leaving both sides with the same ten lines and
a `diff` that fails on ordering alone. They are pulled out and compared as a
sorted set while everything else is still compared in sequence; sorting the
whole log would hide a genuine ordering change, and the load order of the map
and the palette is exactly the sort of thing worth catching.

What it found: `ReleaseSprite` logged "Error in release: Wrong sprite!" where
the original logged nothing. The original tests the register still holding
`table[slot]` from the compare above it — "is the slot occupied by someone
else" — and I had read it as `spr->refs` and then written a confident comment
explaining the wrong behaviour. Nothing reaches that path before shutdown, so
it survived every A/B in the project.

The same run exposed a filter bug worth knowing: `ab.sh`'s counter-dump pattern
was `[A-Za-z_]+`, which does not match a function name containing a digit, so
`BlitCopy16` and friends leaked into the compared log. It could only ever show
up here, because this is the only configuration where the process exits
cleanly enough to dump counters at all.

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

**Its counters read 0 and the movie plays anyway** — do not repeat the mistake
of reading them as coverage. `MovieDrawFrame`, `MovieApplyPalette` and
`SnapshotSystemPalette` all sit behind reconstructed callers (`MoviePoll` calls
the first directly, which calls the second), so none of their counters can
move. Probes show `MovieDrawFrame` past 200 calls and `SnapshotSystemPalette`
twice, on this machine, today.

**The pixel figure depends on when the screenshot lands.** It was 81,494 when
the shot fell mid-film and is 0 when the film has already finished — the run
waits 40 seconds. A 0 here is neither a pass nor a failure; it means the screen
was static at that moment. As with `mission`, the log is the evidence.

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

**The multiplayer path can be A/B'd, so "verified by reading" is weaker than
it needs to be.** START A WAR reaches ENTER BATTLE NAME, which has two text
fields and an OK, so the whole sequence is drivable with `point.py` and
`ctl "type ..."` -- and the same sequence runs under `AM2_NOPATCH=1`.
Screenshots at the same two points came out 90 and 100 pixels apart of 786,432,
which is the blinking caret and the cursor.

That is how `HostBattle` was checked rather than merely read. Its counter reads
1; `CommOpenSession` and `CommCreatePlayer` read 0 because it calls them
directly, which is the usual blind spot and here confirms the path is ours. The
frame after OK matching the original's is the part that matters: the original
also stays on the dialog, so the failure path taken -- DirectPlay will not open
a TCP/IP session on this machine -- is its behaviour and not a defect.

`point.py` needs the exact button centre here. A click two pixels above
MULTI-PLAYER lands between buttons, does nothing, and reads exactly like a dead
code path.

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

**`tools/blindspots.py` says which counters can move, so the question below
does not have to be re-derived every time.** Of 138 traced functions, 44 have
every caller reconstructed and their counters are 0 by construction; 4 more are
reached by address. It gets `WaveCloseReadFile`, `MovieDrawFrame`,
`MovieApplyPalette`, `SnapshotSystemPalette`, `BlitCopy16` and `EncodeGlyph`
right — every case this file has ever had to explain by hand.

It exists because the rule below has been ignored three times, twice in one
session: once putting `WaveCloseReadFile` on a list of things to try harder at
while `StopAudioStream` was calling it all along, and once writing up a whole
commit claiming the intro no longer plays the movie, when `MovieDrawFrame` runs
200 frames at a time. A rule that is written down and forgotten should be
turned into a tool.

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

**`tools/ab.sh mission` is the configuration that reaches live gameplay, and
it exists because `bootcamp` does not.** While either opening dialog is up the
game composes no frames at all — `ComposeFrame` sat frozen at 170 for as long
as the instruction sign was on screen, and the dirty-rectangle merge at
`0x0041D060` never ran once. `bootcamp` stops at the briefing, so everything
behind those dialogs was uncompared. `mission` clears both, then scrolls.

Two things it needed, both of which cost time to find:

- **`tools/point.py` cannot clear the instruction sign.** It finds the pointer
  by colour on a screenshot and that screen defeats it, so every click silently
  did nothing and the counters simply never moved. `drive.sh ctl "mouse left
  tap"` needs no cursor at all, and position is irrelevant when any click will
  do. Reach for the raw button whenever point.py appears to be ignored.
- **The per-frame `-dbg` markers have to be filtered, and the count reported.**
  `-dbg` prints one character per frame during play; over a mission that is
  ~25,000 lines and the two sides never live the same number of frames. The
  first `mission` run compared 24,914 lines against 21,741 and reported a
  difference that was entirely wall-clock. `ab.sh` now strips them through a
  `VOLATILE` filter kept separate from the harness filter — removing our noise
  and removing the game's are different claims — and prints how many went, so a
  filter that ate the whole log could not pass as a clean result.

Its pixel check is disabled, and that is measured rather than assumed: two
unsynchronised runs of a live scrolling mission differ by ~22% of the frame,
which is meaningless by construction. The log is the evidence, as with `intro`.

**The Boot Camp dialogs do dismiss, and getting past them is worth doing.** The
mission opens with MESSAGE FROM HQ over the map — its OK is at roughly
`476,224` — and behind that is a full-screen instruction sign that any click
clears. Past both, the mission is properly live: Sarge on the map, the HUD
drawn, the frame ticking. That is where the interesting counts appear.
`Update3DAudioVolumes` goes from 121 to 9,623 simply by getting the dialogs out
of the way.

It is also where Boot Camp's invariant can be read cleanly: one run gave
`FirstItem` 519 and `NextItem` 835,071, and 519 × 1,609 is 835,071 exactly.

`ESCAPE` does nothing there, and the reason is now mapped rather than
observed. There IS an in-mission ESCAPE handler — `0x00425DA0`, which tests
`!IsKeyDown(ESC) && KeyChanged(ESC)`, i.e. the key being RELEASED, and raises a
menu request. It is arm 34 of a 13-entry sub-state table at `0x00426230`, and
ordinary gameplay is not in sub-state 34, so it never runs. Pressing and
releasing ESCAPE for 1.5 s in a live mission leaves `StopAllSounds` at 0 while
`ComposeFrame` climbs from 8,165 to 22,353 — so this is not a route to the
shutdown path, but not for the reason "there is no handler".

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

- **The Lock/Unlock bracket batch is a different goal from the boundary, and
  its numbers were wrong.** It said "5 of 22 done" and named `DrawText` and
  `DrawSprite` among them; neither calls `LockSurface` or `UnlockSurface` at
  all. Measured: **29 functions** call the bracket and **4** are reconstructed
  — `RenderGlyph`, `RedrawMapRegion`, `CalibratePalette` and `DrawMenuCursor`,
  the last of which the old list predates.

  The sizes quoted for the next candidates were off as well (`0x00413610` is
  128 B, not 256; `0x00433350` is `0x00433360` at 288 B), which is what
  `tools/merges.py` was written to fix.

  Worth being clear about what this item is: the bracket finds the game's
  software RASTERISERS, which are a rewrite goal of their own. It is not the
  Win32/DirectX boundary and finishing it is not required for that boundary to
  be complete — every lock in the image already goes through our `LockSurface`.
  Smallest first: `0x00413610` (128 B), `0x00454F00` (144 B), `0x0041CBA0`
  (160 B), `0x0041CC40` (160 B), `0x0041C7F0` (176 B).
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
  inside something that is otherwise not boundary at all — leaving 3 functions
  and 6 sites, every one a `MessageBoxA` and its `GetActiveWindow` inside menu
  code that no branch in the image can reach. The channels
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
  mutex wait. What is left outside is three `MessageBoxA` calls, and all three
  sit behind copy-protection checks that have been patched to skip them.

  **Those three are a decision and the number stays at three.** `0x0042F290`,
  `0x0044D2E0` and `0x0044D3F0` hold exactly two import sites each — a
  `MessageBoxA` and the `GetActiveWindow` it passes as owner — and no COM
  dispatch at all. Everything else in them is menu logic: sound requests, menu
  state, calls into other game code. Porting them would move pure menu logic
  into the reconstruction to capture a dialog that cannot appear, which is the
  opposite of what ranking by boundary density is for. Measured, not assumed:
  `docs/boundary.md` prints the reasoning with the count.

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
  drift from what the harness installs. What is left outside is **3 functions
  and 6 sites** — three `MessageBoxA` calls and the `GetActiveWindow` each one
  passes as its owner — and all three sit behind CD checks this build has
  patched to jump past them (`docs/binarypatches.md`), so none can execute. The
  other 122 sites are game logic that happens to read a clock or call
  `IntersectRect`.

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
- **Name a function from its body, not from one call site.** Two instances now,
  and the second was still sitting in `orig.h` months after the first was
  written up. `0x0042C0E0` went in as `ADDR_ON_MAP_RESTORED` because
  `RestoreLostSurfaces` tail-calls it; its own error strings say
  `RestoreTileSet`, and it reloads the tileset from a `.atl` file. Renamed.
  `0x0041AD30` went
  in as `AttachPalette` because that is what it looked like where
  `InitDirectDraw` calls it. It is a colour fill — vtable slot 5 is `Blt` — and
  the wrong name survived a commit. Reading the callee costs a minute;
  a wrong name in `orig.h` propagates into every module that picks it up.
- **`tools/checkhooks.py` guards the one failure that no A/B can see.** It
  reads the IAT slot `src/inject/dinput_hook.c` patches, resolves which symbol
  that is from the game's own import directory, and fails if `am2hook.dll`
  imports it. Tested by pointing the hook at `PostMessageA`, which the harness
  does import: it reports the clash and exits 1.

  Worth having because the failure mode is invisible. Both sides of an A/B
  would be equally undriven, so the logs and the pixels would agree perfectly
  while every scripted click went nowhere.

- **A reconstruction can break the harness rather than the game.**
  `src/inject/dinput_hook.c` works by patching the game's IAT slot for
  `DirectInputCreateA`. A reconstructed `InitInput` that imported the symbol
  into `am2hook.dll` would resolve through *our* IAT, walk straight past the
  hook and silently disable all injected input — the game would still run and
  look perfectly healthy. `src/game/win32/device.cpp` calls the game's own import
  thunks (`0x00463396`, `0x00464410`) instead, which read the patched slot at
  call time. Check for a harness hook before reconstructing anything that calls
  an import.
- **Not every reconstruction has to be a patch, and there are two now.**
  `AudioTimerProc` (`0x0040D020`) is the second: `StartAudioStream` hands it to
  `timeSetEvent`, that call is the address's only reference in the image, and
  the call is ours — so a detour would install a jump nothing reaches. Both are
  listed in `tools/coverage.py`'s `REGISTERED`, which `tools/merges.py` imports
  rather than copying, because two lists of "what is done" is how they come to
  disagree. The cost is that neither gets a trace counter, since the counters
  *are* the patch stubs; verify those with a temporary probe.

  `WndProc` is registered, not
  detoured: the only reference to `0x0040A6B0` in the whole image is the
  `WNDCLASS` field in `InitApplication`, and that is ours now. Look for this
  shape before detouring anything reached through a function pointer — a
  callback, a vtable, a dispatch table. It buys back the thing detouring costs,
  which is the ability to defer.

  That deferral has now been taken back: the six messages `WndProc` used to
  forward to the original are reconstructed, so nothing in `winproc.cpp` calls
  `0x0040A6B0` any more.

  **Name a window message from what POSTS it.** Decoding forward from each
  `push <msg>` to the `PostMessageA` that follows gives every sender, and it
  also removes two candidates that a bare constant scan reports: `InitInput`'s
  `push 0x500` is DirectInput's *version number* on its way to `0x00464410`,
  and six `push 0x464` sites are arguments to a CRT call. The senders are
  `PacketThreadProc` for `0x0464` and `0x046B`; `0x00410090`
  ("DestroyPlayer Id=%x"), `0x00411C20` ("TIMING OUT PLAYER") and `CommSend`
  for `0x046C`; `0x00410090` again for `0x046D`; the ready/end-setup handshake
  for `0x046E`; and `AudioTimerProc` for `0x0500`.

  That last one settles something the old names hid: **`0x0500` is not comm
  traffic at all.** It shared a case label with the other five only because
  `WndProc` forwarded them together. The constants are now
  `AM2_WM_PACKETS_READY`, `AM2_WM_NO_BUFFERS`, `AM2_WM_PLAYER_GONE`,
  `AM2_WM_HOST_CHANGED`, `AM2_WM_SETUP_DONE` and `AM2_WM_STREAM_DONE`.

  **Only `0x0500` can be exercised here**, and `AudioTimerProc` posting it means
  it runs in every session: `StopAudioStream` still reads 2 through a Boot Camp
  briefing, which with the forward gone can only have come through our handler.
  The other five need a live DirectPlay session with a second player, so they
  are verified by reading — weaker than the rest of the tree, and worth saying
  plainly.

  `g_charHandler` is NOT the same kind of thing and was left alone. It is a
  slot, not a function: the menu's text fields write their own consumer into
  `0x005125B8` and `WndProc` just calls whatever is there. Porting "it" means
  porting the text-field system — `0x00417790`, `0x00418480`, `0x00454CC0` and
  the two handlers they install — which is about 2.5 KB with no Win32 or COM
  anywhere in it.
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
  Restore without re-locking. Kept as-is deliberately; see `src/game/win32/surface.cpp`.
- Unexercised so far: `RemoveFromItemList`, `KeyFieldC`, `CheckSaveTag`,
  `RestoreTileSet`, and `RefreshScreen` — that last has 7 callers and is
  reached by none of Boot Camp, the intro, the HQ dialog or F1, so whatever
  forces an out-of-band repaint is somewhere further in. `RestoreTileSet` is a
  different case and probably a permanent one: it runs only when DirectDraw
  takes a surface back, which needs an alt-tab or a mode change, and nothing
  under Xvfb does either. Anyone on a real display should alt-tab out of a
  mission and back. `CalibratePalette` came off this list once
  `-w` was understood — it runs twice per windowed startup, and
  `SnapshotSystemPalette` came off this list once the intro movie was allowed to
  play, and stays off it: its counter reads 0 only because its caller is ours.
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

  `FreeDynamicSounds`, `FreeSound` (56 calls) and `ReleaseSoundBuffers` came
  off this list once `tools/ab.sh quit` existed.

  **`WaveCloseReadFile` was never cold — it was the count-of-0 blind spot.**
  `StopAudioStream` is ours and calls it directly, so the counter cannot move.
  A probe shows it running once with a real `HMMIO`. It should not have been on
  this list at all, and the lesson is the one already written above: resolve a
  zero with a probe rather than adding it to a list of things to try harder at.

  `StopNamedSound` and `StopAllSounds` are genuinely unexecuted, and the
  mechanism is now mapped rather than guessed at. `RunFrame` dispatches on the
  state at `ADDR_GAME_STATE` through a table; state 2's handler jumps to the
  level teardown **only when `ADDR_STATE_PENDING` is set**, and that flag is
  raised by `ADDR_REQUEST_STATE` and lowered by `ADDR_COMMIT_STATE`.

  So the teardown runs when a state change is requested while the game is
  *already* in state 2 — on LEAVING a level. Entering Boot Camp is a transition
  into the state and does not trigger it, which is why driving the whole
  title → briefing → mission path leaves both counters at 0; measured, not
  assumed. Quitting from the title screen cannot reach it either.

  The state machine is confirmed rather than inferred: a probe in
  `PollKeyboard`, which runs every frame, shows `0` at startup, `1` on the
  menu and `2` in a Boot Camp mission.

  **`ADDR_LEVEL_TEARDOWN` named the wrong function**, which is why the mechanism
  read as murkier than it is. `0x004260C0` is the state-2 handler — RunFrame's
  jump table at `0x0040B050` dispatches to it every frame of a mission — and it
  tail-jumps to the real teardown at `0x004256F0` when the pending flag is set.
  That one calls `StopAllSounds`. The names are now `ADDR_STATE2_FRAME` and
  `ADDR_LEVEL_TEARDOWN` respectively. Third instance of naming a function from
  a call site rather than its body; the giveaway was already in the file, where
  a comment called it "the state-2 handler at `ADDR_LEVEL_TEARDOWN`".

  The in-game trigger is a **menu request**. `0x00425EE0` consumes the
  `ADDR_MENU_REQUEST` / `ADDR_MENU_REQUEST_SET` pair — the same two globals
  `StartSelectedGame` and `HostBattle` write — and raises the state-pending
  flag. So the chain is: menu request while in state 2 → pending flag → the
  state-2 handler jumps to the teardown → `StopAllSounds`.

  **`StopAllSounds` has now been executed, and the chain above is confirmed by
  running it rather than by reading.** A temporary `poke` command in the control
  socket set `ADDR_MENU_REQUEST`/`ADDR_MENU_REQUEST_SET` during a live Boot Camp
  mission — exactly what the ESCAPE handler writes — and the counter went 0 to
  1, `FreeMapSurfaces` with it, while the game returned cleanly to the title
  with `StartAudioStream("title.wav")` and `CommDropDirectPlay` in the log. So
  the whole path holds: menu request raised while in state 2 →
  `ADDR_TAKE_MENU_REQUEST` consumes it → `ADDR_STATE_PENDING` →
  `ADDR_STATE2_FRAME` tail-jumps to `ADDR_LEVEL_TEARDOWN` → `StopAllSounds`.

  Poking `ADDR_STATE_PENDING` directly also works and is the cruder version of
  the same thing; prefer the menu-request form, since that is the route the
  game itself takes and it exercises `TakeMenuRequest` too.

  Two readings from the same session, both measured. The in-mission sub-state
  `ADDR_MENU_REQUEST_TAKEN` reads **33** throughout Boot Camp play, which is
  why the ESCAPE arm — number 34 — never runs. And `ADDR_STATE_WANTED` really
  does sit at -1 while nothing is pending, as `orig.h` claims.

  **`StopNamedSound` is still unexecuted and is a harder case than it looks.**
  Its only call site is `0x00424DC3`, guarded by the name buffer at
  `0x00511D58` being non-empty. That buffer stays all-zero for an entire Boot
  Camp mission — polled repeatedly — so nothing is ever named to be stopped.
  Forcing a name into it does not help either: the counter stays at 0 through
  90,000 further frames, so the code path holding that call is not reached in
  this mission at all. Note `tools/merges.py` does NOT split the entry at
  `0x00424CA0`, which really is several functions — the call sits past a `ret`
  at `0x00424CD3` — so attributing that site by entry gives the wrong caller.
  A reminder that the split list is a lower bound, exactly as its docstring
  says.

- **`CommOnConnected` (`0x0040E660`) cannot run, and the reason generalises.**
  Its only reference is inside `CommCreateDirectPlay`'s `if (connection)`
  branch, and that function's single caller at `0x0042EE78` passes a literal
  `0`. So the branch is dead and so is everything behind it — including the
  `InitializeConnection` in the same branch. The transport is actually brought
  up by `CommInitializeConnection` from `StartSelectedGame`.

  Worth checking for before spending time trying to exercise something: a
  function can be reachable, called from live code, and still never run because
  the argument that gates it is a constant at the one call site.
- **The script family confirms the count-of-0 blind spot rather than
  contradicting it.** A Boot Camp run reads `ScriptNextToken` 101 and
  `ScriptResetTokens` 1, while `ScriptLookupToken`, `ScriptAddToken`,
  `ScriptGrowTokens`, `ScriptParseNumber`, `IsBlank` and `IsScriptDelim` all
  read 0 -- our `NextToken` calls them directly and never crosses a patched
  entry. The same run gives `FirstItem` 363 and `NextItem` 584,067, and
  363 x 1,609 is 584,067 exactly, so the registry invariant holds with the
  tokeniser in place. `tools/ab.sh mission` is clean on the same build.
- The interpreter's top-level grammar is mapped and one of five handlers is
  written. `ReadScript` dispatches on exactly six ids, which is also the set
  `ScriptIsStatementStart` answers yes for: `preloadsprite` (25) ->
  `0x00444900`, `pad` (26) -> `0x004440E0`, `if` (44) -> `0x004432F0`,
  `variable` (133) -> `0x00443F70` (**done**), and `object` (139) and
  `objclass` (140) sharing `0x00436D60`. The four left are 2,080 B, 2,896 B,
  400 B and 688 B, and they reach into sprite loading, object creation and an
  8,608-byte expression evaluator at `0x00440D70` -- that last is also what
  `ParseLine` (`0x00444C40`) needs, which is why ParseLine is not done either.
  `GenerateObjScriptFromTokens` is a real source name, recovered from a string.
- Object types 2, 3 and 8 are still unidentified.
- `object.aai` complains about `link 33-1..4`; unexplained.
