# Working on this repo

Standing conventions and decisions for the Army Men II reconstruction. This file
is the part that is *policy* rather than discovery: the things that would
otherwise have to be re-litigated, or re-learned the hard way, on every new
machine and in every new session.

**Findings and status do NOT belong here.** What the binary is, how each
subsystem was reconstructed, every measurement, war-story, and open item lives
in `docs/` and `STATUS.md` (indexed under **Where findings live** at the end).
When you learn something, write it there; edit this file only when a standing
rule, convention, or decision changes. This file was 4,000+ lines of accreted
discovery once — keep it from becoming that again.

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

**`make check` runs everything that does not need the game.** **70** analysis
tools plus a drift check that fails if any generated file under `docs/` no
longer matches what the tools produce. The list is in the `check` recipe; it
said "eight" here for a long time after it stopped being eight, and then said
"fifteen" while the recipe ran seventeen -- a warning about stale numbers is
not a defence against one. `checkclaims.py` counts the recipe now, so this
sentence cannot drift again.

One of them is `tools/checkclaims.py`, which reads the numeric claims out
of *this file* and recomputes them. It exists because three separate figures
here were found stale by measuring rather than reading, each from the same
cause: a tool changed, some prose was updated, the rest kept asserting the old
number. It is deliberately short — most of this file is judgement and cannot be
checked, which is the argument for keeping numbers in `docs/boundary.md` and
pointing at them from here. Seconds, no display, and it is the half
of verification `tools/ab.sh all` is not.

`checkclaims.py` also guards its own `ORACLES` list (a tool that declares
`CHECKS` but is missing from `ORACLES` is an ORPHAN and fails) and two of
`STATUS.md`'s numbers; the third (the patch count) is deliberately left to
`checkpatches.py`. The drift check catches a generated file that wasn't
regenerated, but a hand-edit to a generated file is healed silently — know that
before trusting a green run. The incidents behind these are in `docs/tooling.md`.

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

**Two module names come from the image itself.** It carries
`C:\ArmyMen2\source\script.cpp` and `C:\ArmyMen2\source\objscript.cpp`, and
the functions referencing each sit in bands of their own -- objscript around
`0x004364A0..0x004375A0`, script from `0x0043EE80` up. The reconstruction is
split the same way rather than along a line of our choosing, with
`scriptint.h` as the private surface between them. Where the original's own
division is visible, use it.

`src/game/win32/` holds every module that talks to Win32 or DirectX -- **16**
of them. The flat part of `src/game/` holds the reconstruction that touches no
API at all, and there are **35**; the split is the answer to "what still talks
to the outside world" in directory form.

**The flat half is the one that grows, and this file's count of it went stale
without anything noticing.** It said eight, naming `blit`, `dist`, `objtable`,
`objtype`, `packkey`, `rect`, `savetag` and `text` -- software rasterisers,
rectangle and distance maths, the object tables, key packing and save tags:
pure computation over memory the caller supplies. Those eight are still there
and still flat, so the sentence was not wrong about them. What it could not
survive was fifteen more landing beside them -- the script interpreter, the
event table, the object accessors, the save serialisation, and map.cpp,
pad.cpp, air.cpp and gameproc.cpp, which the image names in the strings it
hands CheckSaveTag -- while
the prose went on counting the original set and calling everything else the
boundary.

`tools/checksplit.py` was checking the only thing it could see, which is that
each module is on the correct side; nothing was checking how many there were.
Both counts are `tools/checkclaims.py`'s now, for the same reason every other
number in this file that kept going stale is.

The test for which side a file belongs on is whether it names a Win32 or COM
type at all. `blit.cpp` mentions `IDirectDrawSurface` once, in a comment
explaining where the original's fallback came from, and stays flat; it operates
on a locked pointer somebody else obtained.

Includes are written out in full rather than resolved by `-I` flags, so a
module's directory is visible at its use sites: `win32/` sources reach the
harness as `"../../inject/orig.h"` and the flat half as `"../blit.h"`.

## Language split

`src/game/` is **C++** (`.cpp` sources, `.h` headers). `src/inject/` is **C** —
it is harness, not game.

The reason is an ABI survey: of the 1,239 game functions below the CRT,
**100 are thiscall**, which on i386 means non-static member functions.

The 100 is history (nothing recomputes it today; `checkabi.py` now only audits
reconstructed functions against a hand-kept table) but the argument holds:
reconstructing thiscall members in C would mean hand-written
`__attribute__((thiscall))` shims around what the original wrote as methods.

Consequences to remember:

- Every `src/inject/*.h` that declares a function shared with `src/game/` needs
  an `extern "C"` guard. `win32.h`, `orig.h` and `sites.h` declare none and must
  be left alone — wrapping `win32.h` in particular would be wrong, since it
  pulls in `windows.h`.
- C++ will not implicitly convert a function pointer to `const void *`, which is
  what `patch_replace` takes, so install sites cast explicitly.

## Differential testing

Two harnesses compare a reconstruction against the original with no whole game
running, and both are in **`docs/verification.md`**:

- **`AM2_SELFCHECK=1`** calls each reconstruction and the original side by side
  inside the running game, before `install()` patches anything.
- **`make selftest`** (`tools/vectors.py` + `tests/selftest.cpp`) emulates the
  original under Unicorn, records input→output vectors, and replays them against
  our C++ — for functions pure over memory a stub or the mapped image supplies.

Where the input space is small, an **enumerating oracle** beats sampling; the
family of them (`moviecheck`, `posecheck`, `shakecheck`, `scriptcheck`, …) and
the rules for writing one are in `docs/verification.md` and `docs/oracles.md`.

## The script interpreter

The game ships its missions as readable text (`data/<map>/<map>N.txt` and
`rules/*.txt`), so this subsystem's names come from the program's own
vocabulary. The whole parser and all five statement handlers are reconstructed;
what is still original below them is engine (DirectDraw, comm, bitmap loading,
event dispatch), reached *through* the handlers. The chain, the token/name-table
layouts, and the parse-in-the-game oracle (`AM2_PARSE_ALL`) are in
**`docs/script.md`**; the generated token and action tables are
`docs/scripttokens.md` and `docs/scriptactions.md`.

## The WinMain chain

The twelve functions between `WinMain` and the engine are reconstructed, and so
is everything `RunFrame` reaches below them (the input poll, the two comm
bookkeeping steps, and all five per-state handlers, in
`src/game/win32/frame.cpp`). The findings behind that — the menu-request
dispatch table, the pause mask, the eight-arm AI-mode jump table, the level
teardown chain, and the many naming corrections — are in **`docs/winmain.md`**.

## After writing a function, in this order

Each of these caught something in one session that reading had already missed,
which is why the list is empirical rather than aspirational.

0. **READ THE WARNINGS, AND NEVER `grep` THE BUILD FOR `error`.** This
   file's step 1 says the compiler is the first check, and then a session
   spent all day running `make -s 2>&1 | grep -E 'error'`, which throws away
   every warning the step exists to read. It cost a whole function:
   `RowPoolAlloc` was written, compiled, committed and installed nowhere, and
   GCC said so at the time -- `'RowPoolAlloc' defined but not used
   [-Wunused-function]` -- into a filter that discarded it.

   `-Wall -Wextra` is already on, so the diagnostic was free. What was not
   free was the two commits and an A/B run spent believing eight functions
   had landed when seven had. **`-Wunused-function` on a static is the
   compiler telling you a reconstruction is not wired up**, which is the one
   failure `checkinstalled` and `checkpatches` are both blind to: there is no
   declaration for the first to match and no patch for the second to count.

   Note `-fsyntax-only` does NOT emit it -- the analysis needs a real
   compile. `g++ ... -c -o /tmp/x.o src/game/foo.cpp` outside the build tree
   answers in a second without disturbing a running suite.

1. **`make`.** The compiler finds name collisions -- three in one session, twice
   because the obvious name for a table was already on a different table -- and
   linkage mismatches, which go BOTH ways: `mapdraw.h` closes its `extern "C"`
   at line 129 and declares `ShakeAt` at 158, so a stub for it must NOT be
   wrapped, while `BoatExitPoint` must be.
2. **Read the transcription count.** A function can be written, compile clean,
   and pass every static check while never being installed -- `checkinstalled`
   cannot see one whose declaration was never added. That happened twice in one
   session. **The count moving is the only proof the patch went in**, and an
   A/B over an uninstalled function compares the original against itself and
   reports clean.
3. **`make check`.** `checkseams` finds an `orig_` macro on reconstructed code
   -- and its TYPEDEF is evidence: twice it exposed a wrong arity, and once the
   arity error was hiding a wrong gate condition. `checkoffsets` refuses a
   second name on an offset that already has one.
4. **`tools/checkoffsetuse.py <addr> <src> <Func>`, BEFORE the A/B.** It diffs
   the displacements the original reads against what the C's `*_OFF_*` macros
   expand to. It has caught unnamed literals and, once, OMITTED CODE -- a
   cleared field and a whole second exit. It costs seconds and does not depend
   on any drive reaching the function. Read its docstring for the eight blind
   spots; four produce false positives, which is why it is a report and not a
   gate.

   **And it read the C's COMMENTS, which is how it reported a macro the
   function does not use.** A note saying "+0x540, not `OBJ_OFF_SOLDIER_KIND`
   at +0x544" put 0x544 in the C's set and the tool flagged it as unread by the
   original -- true, and about the prose rather than the code. Comments are
   stripped now. `tools/checkseams.py` had to learn exactly this and went from
   about two hundred false sites to 21; any check that scans this tree's source
   has to, because the comments discuss every name in it.

   **It matched only `__cdecl` definitions, so it had never seen a thiscall
   one -- which is the entire menu widget layer.** Every `*Construct` and every
   vtable slot answered `no definition of <name>`, and that reads like a typo
   in the invocation rather than a gap in the tool, so it was never chased.
   Found by pointing it at a new constructor and then at one written long
   before, which gave the same non-answer. The regex takes
   `__attribute__((thiscall))` now. What it reports on those is dominated by
   STRUCT MEMBERS, already one of its documented blind spots: `AM2_Widget`'s
   `x`/`y`/`w`/`h` at 0x4/0x8/0xc turn up in every constructor in the family,
   because a `RECT` passed by value is copied a dword at a time.
5. **`tools/ab.sh`, and read the STATE artifact rather than the verdict line.**
   `bootcamp`'s state dump is 1,610 lines of object fields diffed with no
   budget, and it is what catches a wrong field where the pixels and the log
   agree.

**Before writing, ask four things in this order, not by size.** Byte count
predicts transcription volume; these predict reading cost, which is where the
defects are.

  - **Can anything execute it?** A comm function with six of seven callees
    reconstructed still cannot be A/B'd on a machine that opens no DirectPlay
    session.
  - **How many callees are unnamed?** Each is a function that must be read
    first. A 448-byte function with six unknowns is dearer than a 560-byte one
    with one.
  - **How many exits, and do they converge?** Neighbours in a family are not a
    guide: `StepType2` converges to one tail and `CanPickUpWeapon` has eight
    independent returns. Assuming either way costs a rewrite.
  - **What do the dispatch tables CONTAIN?** Counting entries said "two 29-way
    dispatches"; reading them said "one six-element predicate used twice".

**And classify the counter before writing, not after.** A zero means blind
(every caller reconstructed), cold (nothing reaches it), or unwatched -- and
this file records time lost to picking the wrong one. `StepType6`'s caller is
ours, so its counter cannot move; `FormationSlotPoint`'s callers are original,
so its zero was real and a `TRACE=1` probe proved the function never runs.

**Reconstruction pitfalls — the rules; the case studies that produced each are
in `docs/reconstruction-lessons.md`.** Every one of these compiles, passes the
static checks, and is invisible to an A/B, because each indexes or returns
*something*:

- **Grep the ADDRESS (bare hex), not the name, before naming a global** — and
  before writing an ACCESS expression for it, grep how the tree already reaches
  it. `orig.h` may already name it, or reach it through a different indirection
  depth.
- **A field pointer is not a table base.** A consumer that touches one field
  indexes correctly from any base that puts that field where it expects; ask
  what is BEFORE the field, and prefer the writer/reader over a `free` site.
- **Name a function from its BODY (and its callee), not from one call site** —
  a callee reached from a caller's default arm is the general case, not the
  caller's special one.
- **Read the call site twice: once for arg COUNT (count the pushes; `add esp`
  and thiscall callee-cleanup both lie), once for arg IDENTITY (what each
  caller register HOLDS).** The body shows only which args are used.
- **Write the exits first, from the epilogues, and count PATHS not `ret`s** —
  an early return that also skips a shared tail is a dropped call.
- **Verify indirection DEPTH, not just the addressing mode** — `array →
  records[0] → sprite → key` is three loads; a byte-check of one instruction
  cannot see the chain length.
- **A new offset PREFIX is invisible to `checkoffsets`** (it compares within a
  prefix); grep the offset before opening a new family.
- **One predicate asked twice can be used in opposite senses** — two calls are
  two branches to read.

## Verifying a reconstruction

The drive how-to is below; the per-configuration knowledge (bootcamp, mission,
audio, intro, multiplayer, `-df`, quit), the enumerating oracles, and the
pixel-budget lessons are in **`docs/verification.md`**, `docs/abnotes.md`, and
`docs/oracles.md`.

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

## Driving input

**`drive.sh ctl "cursor X Y"` places the pointer, absolutely, in one round
trip.** Reconstructing `UpdateMouseState` is what made that possible: the
absolute cursor DirectInput's deltas were feeding is a global, and `cursor`
writes the three the image reads — `ADDR_CURSOR_X`, `ADDR_CURSOR_Y` and the
packed `ADDR_CURSOR_POINT` that 32 sites test the pointer against. With no
argument it reports where the cursor is. `tools/point.py` is a thin wrapper
over it and keeps its old command line.

**The globals are the GAME's, so this works unchanged under `AM2_NOPATCH=1`** —
verified by driving the same click both ways and landing on the same screen.
That is what makes it usable for an A/B: both halves take identical
coordinates, where a relative delta depended on acceleration and arrived
somewhere slightly different each run.

What it replaces, and why the replacement matters rather than being a tidy-up.
The game reads BUFFERED DirectInput `GetDeviceData`, not `GetDeviceState`, so
the socket could only offer relative motion; Wine's acceleration is non-linear
on top (~1.75× for a 100-pixel step, ~2.0× for a 300-pixel one), so a computed
delta overshot. `point.py` closed the loop on a screenshot, finding the pointer
by colour, with a threshold sampled from a real frame because a loose one
matched title-screen dirt at (181,156,88) and reported rubble as the pointer.

**It could not work at all where the cursor is not drawn, and failed
silently.** The Boot Camp briefing screen and the full-screen instruction sign
both defeated it — every click went nowhere and the counters simply never
moved, which reads exactly like a broken reconstruction. That is the failure
this removes. Driving title → BOOT CAMP → `RETURN` → both dialogs → live
mission is now four socket commands, and `Update3DAudioVolumes` reads 16,323
at the end of it.

**`drive.sh ctl "keys"` is the same idea for the keyboard, and it is a READ
only.** It reports the game's own state: which scancodes are down, following
`ADDR_KEYS_NOW_PTR` because `PollKeyboard` SWAPS the two 256-byte buffers each
poll so which one is current alternates, and which registered a press through
`ADDR_KEY_PRESSED`, the edge-and-auto-repeat array most of the game actually
tests. Works under `AM2_NOPATCH=1` for the same reason `cursor` does.

**`state` is intent and `keys` is outcome, and the difference is the point.**
`state` reports what the HARNESS is injecting. If the DirectInput hook were
ever bypassed — the failure `tools/checkhooks.py` guards, which no A/B can see
because both sides would be equally undriven — `state` would keep reporting a
key as held while the game saw nothing. `keys` reads the other end of the
channel and would show it.

**There is no way to SET the keys, and that asymmetry is real rather than
unfinished work.** The cursor ACCUMULATES: `UpdateMouseState` adds the deltas
to what is already there, so a write survives and becomes the next starting
point. The key buffer is REPLACED wholesale from `GetDeviceState` on every
poll, so a poke would last one frame. Keys go in through `key`, and the
harness already releases a timed hold on a poll rather than from a timer,
precisely so a tap cannot fall between two polls. Owning the reconstruction
does not make every global writable; ask whether the game accumulates it or
overwrites it.

**Two globals renamed on the way**: `ADDR_INPUT_CURSOR_A`/`_B` are the
keyboard's buffer pointers and have nothing to do with the mouse — "cursor"
meant a cursor into a buffer, which sitting next to `ADDR_CURSOR_X` is a trap.
They are `ADDR_KEYS_NOW_PTR` and `ADDR_KEYS_PREV_PTR`. Renamed, not aliased:
the count in `checkpatches.py` stayed at 39.

**Anything gated on `g_mouseMoved` needs `mouse move`, and `cursor` will
silently skip it.** The widget layer's hover paths all test that global before
they look at the pointer, so poking the cursor across three list rows with
`cursor` moved `BlinkerStart` not at all, while eight relative movements over
the same rows took it from 2 to 10. The counter reading 0 looked exactly like
"this code never runs" and meant "this input never arrived".

**`mouse move DX DY` is still the honest way in when the input path is what is
under test.** `cursor` writes the globals and reads nothing from the device, so
it exercises neither `PollMouse` nor `UpdateMouseState`. `ab.sh mission` scrolls
with relative motion for exactly that reason.

## Build and install hazards

Header dependencies are tracked with `-MMD -MP`. Do not remove this. The build
originally compiled every source in one command, so header edits were always
picked up; splitting into per-object rules silently lost that, and the symptom
is edits that appear to do nothing.

`install-hook` copies to a temp name and `mv`s into place. **Never overwrite a
mapped DLL** — a plain `cp` corrupts a running instance.

When killing the game, bracket the pattern: `pkill -f 'ArmyMen2[.]exe'`. Without
the brackets the pattern matches the killing shell itself.

**Killing the game safely — the rules, incidents in `docs/tooling.md`.** After
any kill, check BOTH that no game process survives and that the control port is
free before believing the next run — a survivor holds `ArmyMenMutex` (the next
run exits) or port 31436 (the next run ignores every command), and both read
exactly like a broken reconstruction:

```
pgrep -c -f 'ArmyMen2[.]exe'   # want 0
ss -ltn | grep 31436           # want nothing
```

Match on the EXE name and exclude the caller's own ancestor chain; do not add a
predicate about how it was launched (the launcher's command line is the game's
own path). **Wait on a PID, never on a pattern the waiter's own arguments
contain**, and use `tools/pidfd.py wait PID [TIMEOUT]` / `pidfd.py kill PID
[SIG]` — a pidfd names one process, cannot reach a reused pid, and kills the
descendants a wrapper `$!` would miss. The session-kill, self-matching-waiter,
and reparented-tracer incidents that produced these rules are in
`docs/tooling.md`.

## Where findings live

This file is policy. Everything else — the running open-items list, per-subsystem
findings, measurements, and status — lives in `STATUS.md` and the topic docs:

| subject | doc |
|---|---|
| what the binary is / the harness | `docs/00-recon.md`, `docs/01-harness.md` |
| the Win32/DirectX boundary (counts) | `docs/boundary.md`, `docs/boundary-notes.md` |
| the WinMain → engine chain | `docs/winmain.md` |
| the script interpreter | `docs/script.md`, `docs/scripttokens.md`, `docs/scriptactions.md` |
| verifying: drive configs, oracles, A/B | `docs/verification.md`, `docs/abnotes.md`, `docs/oracles.md` |
| frame-exact comparison (hybrid/lockstep/heap) | `docs/lockstep.md` |
| the CRT reconstruction | `docs/crt.md`, `docs/crt-notes.md` |
| analysis-tool notes, build/kill hazards | `docs/tooling.md` |
| reconstruction pitfalls (case studies) | `docs/reconstruction-lessons.md` |
| save/load, movement, combat, objects, tables | `docs/saveload.md`, `docs/movement.md`, `docs/combat.md`, `docs/objects.md`, `docs/tables.md` |
| screens, cheats, binary patches, Lua console | `docs/screen-mpoptions.md`, `docs/screens.md`, `docs/cheats.md`, `docs/binarypatches.md`, `docs/lua.md` |
| current state, open items, measurements | `STATUS.md` |

**Keep it that way.** A new finding, measurement, war-story, or status update
goes in the relevant doc or `STATUS.md`, never here. This file changes only when
a standing rule, convention, or decision does. A doc that receives a claim
`tools/checkclaims.py` recomputes, or a present-tense "still original" address
sentence `tools/checkprose.py` validates, joins that tool's file-list in the
same commit.
