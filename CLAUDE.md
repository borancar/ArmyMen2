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

**`make check` runs everything that does not need the game.** **40** analysis
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

**Two module names come from the image itself.** It carries
`C:\ArmyMen2\source\script.cpp` and `C:\ArmyMen2\source\objscript.cpp`, and
the functions referencing each sit in bands of their own -- objscript around
`0x004364A0..0x004375A0`, script from `0x0043EE80` up. The reconstruction is
split the same way rather than along a line of our choosing, with
`scriptint.h` as the private surface between them. Where the original's own
division is visible, use it.

`src/game/win32/` holds every module that talks to Win32 or DirectX -- **16**
of them. The flat part of `src/game/` holds the reconstruction that touches no
API at all, and there are **34**; the split is the answer to "what still talks
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

**A FIELD POINTER NAMED AS A TABLE BASE IS THE COMMONEST MISTAKE IN THIS
PROJECT, AND FOUR MORE LANDED IN ONE SESSION.** `ADDR_RANK_RECORDS` was four
bytes late, `COMM_OFF_PLAYERS` twelve, the roach step's "control record" was a
window into an object, and `ADDR_ARMY_INK` was byte 1 of
`ADDR_OBJ_TABLE_RECORDS`. Only the first two predate the session; the last two
were introduced BY the person writing this warning, one of them within a single
batch of correcting another.

The shape is always the same and it is always invisible at the time: a consumer
that touches ONE field indexes correctly from any base that puts that field
where it expects. Nothing can see the error until a second consumer wants an
earlier field.

What actually catches it, in order of how often it worked here:

- **Grep the ADDRESS before naming anything**, and grep it as a bare hex
  number, not as a name. Three of the four would have been caught by one
  `grep 0x004F9AC` before the `#define` was written.
- **Ask what is BEFORE the field you are naming.** A record whose "first" field
  has things written twelve bytes below it is not starting where you think.
- **A stride you have to define is a stride that probably already exists.**
  `AM2_ARMY_INK_STRIDE 0x100` was a second spelling of `AM2_OBJ_TABLE_REC_SIZE`
  and that alone should have stopped the edit.

**RE-BASING A SHARED MACRO NEEDS AN AUDIT, NOT A GREP.** Moving
`COMM_OFF_PLAYERS` twelve bytes to the record's real start meant every use had
to gain a compensating field offset. One of twenty-six was missed --
`widget.cpp:7890`, wrapped across two lines so a `grep -n 'rec + COMM_OFF'`
never saw it -- and it silently wrote a computer player's name twelve bytes
early. `ab.sh mpoptions` caught it outright: the widget tree lost rows, the
chat record came back empty and the frame moved 253,227 pixels.

The fix is a script, not more grepping. Walk every use with its next two lines
and assert each one carries a field offset or a stride; that check is three
lines of Python and it found the one that eyes did not. **A macro whose VALUE
changes is not a rename** -- it is a change to every site at once, and the
sites are what have to be enumerated.

The control mattered here too, and nearly lied a second way: the first two
control runs failed with "produced no game log lines" because a stale
`ArmyMen2.exe` still held port 31436 from the failing run. Check `pgrep` and
`ss` before believing a control, which this file already says and which is easy
to skip when you are chasing something else.

**`tools/checkoffsets.py` has one blind spot and it is the PREFIX, which was
demonstrated rather than reasoned.** One edit created two duplicates of the
same two fields: `ITEMTYPE_OFF_COOLDOWN`, which already existed at the same
value under the same prefix and was refused outright, and
`WEAPON_OFF_LAST_FIRED` for `0xC4`, which the tree already had as
`ITEM_OFF_LAST_USE`. Only the first was caught. A NEW prefix is a new namespace
and the checker has nothing to compare it against, so the second would have
landed silently and been a second name for one field forever.

So "grep the OFFSET before naming it" means the offset, not the prefix -- and
the case where it matters most is exactly the case where the prefix is new,
because that is when the checker cannot help. `0xC4` alone would have found it
in one command.

**Sharing a VALUE is not the same as being a duplicate, and only one of the
two is worth collapsing.** `orig.h` holds three names for 4 --
`AM2_COMM_SLOTS`, `AM2_COMM_ARMY_COUNT`, `AM2_REVEAL_ARMIES` -- and four for
15, and none of those is a defect: comm slots, armies and reveal grids are
three concepts that happen to number the same, and a footprint's weight step
is not a passability threshold. What IS a defect is one concept under two
spellings, which is what `AM2_TILE_NEIGHBOURS` and `AM2_TILE_NEIGHBOUR_COUNT`
were -- the same table's bound, both in use in the same file. Collapsed. Any
tool for this class has to key on the concept, not the number, which is why
there is no ratchet and probably cannot be a cheap one.

**Offset macros have no ratchet at all, and that is where the fourth duplicate
of the session landed.** `checkpatches.py` counts `ADDR_` aliases and
`checkglobals.py` counts `g_` ones; nothing counts `OBJ_OFF_*`, `AM2_*` or the
other plain constants. So when `OBJ_OFF_ROW_COUNT` and `OBJ_OFF_ROWS` were
defined a second time with the same values, and `AM2_ROW_STRIDE` invented
beside the existing `AM2_OBJ_ROW_STRIDE`, everything compiled and every check
passed -- an identical redefinition is legal C, and the third name is simply a
different spelling of a number.

It was found by reading `TakeOffMap`, which had been using the originals all
along. Until something counts them, the only defence is the same one the
`ADDR_` rule states: grep for the OFFSET before defining a name for it, not
just for the name.

**THE NEXT PROSE RATCHET WAS TRIED AND IS NOT WORTH BUILDING, which is worth
saying so nobody builds it twice.** After checkprose.py the obvious extension
is to flag MACRO NAMES cited in the docs that no longer exist in `src/` --
this file names plenty, and several were renamed in one session
(ADDR_EVT_PAD_HANDLER_A, ADDR_BATTLE_JOIN_DRAW, ADDR_COMM_SYNC_CHECK,
COMM_OFF_SEND_COARSE). Scanned: fifteen hits, and not one is a defect.

Eleven are deliberate HISTORY -- ADDR_STARTUP_4249C0, ADDR_GAME_DIR_ALT and
the rest of the alias table exist in this file precisely because they were
deleted, and AM2_ROW_STRIDE and AICTX_OFF_OBJ_10 are named as duplicates that
were collapsed. Removing those mentions would delete the finding. The other
four are the scan's own fault: `ADDR_STR_` is a prefix rather than a macro,
and AM2_AB_PIXELS, AM2_AB_TRACE and AM2_MAKEVARS are environment variables,
all three real and all three defined in tools/ rather than src/.

So the class is dominated by correct usage, and a check over it would be a
noise generator with a caveat attached -- which this file already says is
worth suspecting before it is worth believing. checkprose works because
"this address is still original" is a claim that can only be true or false;
"this name is mentioned" is not a claim at all.

**`tools/checkglobals.py` ratchets the `g_` macros, and there is a large
backlog behind it.** `src/game` reaches the original's globals through macros
like `#define g_defaultOwner (*(uint32_t *)(uintptr_t)ADDR_DEFAULT_OWNER)`, and
nothing was checking them at all — so one global ended up with four definitions
across three types, one of them a second name. The `ADDR_` ratchet in
`checkpatches.py` would never have seen it: the `ADDR_` name underneath is the
same in every case, so grepping the address finds them all and they look
consistent.

Two rules, both already applied one level up. An **alias** is one address
reached through two `g_` names. A **drift** is one `g_` name defined with two
different expansions.

It found **38 surplus names and 17 surplus spellings** on its first run, so it
is a baseline that may only go down, not a clean bill of health. Some of the
backlog is harmless const-vs-non-const; some is not.

**The example this paragraph used went stale by being fixed, which is the good
way for prose to go wrong and still worth correcting.** It named
`ADDR_FONT_SURFACE`, reached through five names including `g_backBuffer` and
`g_fontSurface`, and said settling it meant reading what the surface is for
rather than renaming the loser. That was done: `InitDirectDraw` takes it off
the primary with `DDSCAPS_BACKBUFFER` when fullscreen and makes a plain
offscreen surface of the same size when windowed, `PresentFrame` blits it to
the primary, and it is `ADDR_BACK_BUFFER` now. Read the tool's own output for
what is outstanding, never this sentence.

What the backlog actually holds today is mostly SPELLING rather than
misreading, and that is a cheaper job than the one described above:
`g_originDX` beside `g_originDx`, `g_originDY` beside `g_originDy`, `g_curX`
beside `g_cursorX`, `g_clipRect` beside `g_screenClip`, `g_comm` beside
`g_commObject`. Those need no reading at all — they need one spelling. The ones
that do need reading are the DirectSound buffer slots, where `g_dsBufA` and
`g_dsPrimary` are on one address and `g_dsBufC`, `g_dsound` and
`g_movieDSound` on another, and a name there is a claim about which object it
is.

**Count the surplus, not the addresses that have any.** The first version
counted addresses with more than one name, and a fifth name landing on an
address that already had four did not move it — which is exactly where a new
alias is most likely to appear, and it passed when tried. Counting
`len(names) - 1` fails in both directions, tested both ways.

**`am2.Image.refs_to` cannot see a call, and reading it as though it could is
how three separate things in this file were nearly recorded as dead code.** It
scans a section for the target as a little-endian dword, which finds vtable
slots and `push imm32` operands and nothing else — `call rel32` and `jmp rel32`
encode a displacement, so the address is not in the byte stream at all. Asked
about `LockSurface`, which has 38 call sites, it answers **0**.

The failure is quiet and it is convincing: a survey of all 33 menu widget
classes came back "not one is ever instantiated", which is nonsense the moment
you look at a menu. `am2.Image.xrefs` decodes instead and is the method to
reach for; `refs_to` answers the narrower question and now says so in its
docstring. The check that either is working is to point it at a function whose
call count is already known.

**`tools/checksplit.py` keeps the split honest, in both directions.** It fails
if a flat module names a Win32 or COM type — or reaches a Win32 header
transitively, since a header can be the thing that leaks — and equally if a
`win32/` module names none, which would mean something is filed as platform
code that is not. Tested both ways round: a bare `HWND` in `rect.cpp` and a
`frame.cpp` with its three platform references renamed away both fail it.

Comments are stripped before scanning, and that is not fussiness. `script.cpp`
carries a comment saying it forward-declares `PreloadSprite` rather than
including `win32/sprite.h` precisely BECAUSE `AM2_Sprite` has an
`LPDIRECTDRAWSURFACE` in it — a scan that reads comments fails the very file
that documents the rule.

**Four tools derive "what is reconstructed" by scanning these sources**, and
all four used a non-recursive `listdir` before the split. Adding a
subdirectory would have made every one of them miss the whole `win32/` half
silently and report the boundary as barely started. They now share `am2.game_sources()`
— one definition, for the same reason `tools/merges.py` imports
`coverage.REGISTERED` rather than copying it.

## Language split

`src/game/` is **C++** (`.cpp` sources, `.h` headers). `src/inject/` is **C** —
it is harness, not game.

The reason is an ABI survey: of the 1,239 game functions below the CRT,
**100 are thiscall**, which on i386 means non-static member functions.

**THE CITATION WENT STALE WITHOUT THE CLAIM DOING SO, and only half of it can
still be checked.** This sentence credited `tools/checkabi.py`, and that tool
no longer performs the survey -- it audits the calling convention of every
RECONSTRUCTED function against a hand-kept table, which is a different and
narrower job. Measured today: the denominator is exactly right, 1,239 entries
below CRT_START. The 100 is not produced by anything in the tree any more; a
crude re-scan (ecx read before written, edx not, `ret N`, first 256 bytes)
finds 57, which is a LOWER BOUND rather than a contradiction, because that
heuristic is weaker than the one that produced the original figure.

So the number stands as history and the argument it supports is unaffected --
a hundred or fifty-seven thiscall functions both make the case for C++ -- but
nothing checks it, and this note is here so the next reader does not run
checkabi.py expecting to see it. Reconstructing those in C would mean hand-written
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

**And it was comparing every pointer-argument function against ONE input, with
both pointers at the same bytes.** Two defects in one line of `fill_scratch`:
it did not vary with the iteration, so all 128 calls saw the same memory; and
`(i * 7 + 13) & 0xFF` has period 256 while the pointer arguments are 0x100
apart, so every pointer pointed at IDENTICAL bytes. `ApproxDist`,
`PointInRect`, `PointsEqual` and every `Obj*` predicate were being handed two
copies of one value.

This is the offline harness's own bug — `tools/vectors.py` hit it with a stride
of 0x800 and fixed it with a salt — and the in-process one had it with 0x100
and nobody had looked. Both harnesses are worth checking against each other's
scars.

Found by mutating a function and watching the run pass, which is the only way
these ever surface. Re-run after the fix: 0 disagree, so nothing had been
relying on the accident.

**A FIELD WITH NO WRITER IS DEAD CODE, and the scan that says so is cheap.**
`SendGameMsg` has two packet-loss arms behind a player record's `+0xA4` and
`+0xA8`, and both look perfectly live -- a burst mode and a random mode, a
percentage, an exemption for guaranteed sends. Nothing in the image writes
either offset. A decoded scan of every store in `.text` finds twelve hits at
those two displacements and all twelve are other structures or `esp`, so both
read zero forever and neither arm can be taken.

That is worth doing BEFORE reasoning about what a field means, not after. The
sibling pair at `+0xB0`/`+0xB4` in the same function passes the same scan with
a writer -- `RecvFlowControl`, the host dictating latency to a client -- and
that is what turns two letters into `FLOW_OFF_LAG_MS` and `FLOW_OFF_LAG_SPREAD`.
One scan separated the half that can run from the half that cannot.

**A function whose answer depends on a table the game has not built yet needs
the table SEEDED.** `AngleBetween` reads the two reverse trig tables, which are
`.bss` zeros when the selfcheck runs — so every index answers 0 and the
indexing, which is the whole function, goes unchecked. `fill_atan_tables()`
writes a position-dependent byte into each of the 2,050 entries first; the game
overwrites both at startup, so the scribble cannot outlive the check.

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
433 unreconstructed leaves qualified**, and 99 of those already yielded
vectors.

**BOTH FIGURES ARE HISTORY NOW AND THE TENSE MATTERED.** There are no
unreconstructed leaves: tools/remaining.py reads 0 game functions and 0 static
initializers. The sentence was written when the denominator was live and went
on describing a population that has since emptied, which is the same drift as
a stale count except that no number changed -- the WORLD did. The tool still
does exactly what this paragraph says; what is gone is the set it was pointed
at, so its remaining job is the validation set, not a queue.

**A vector count that includes DUPLICATES is a claim about effort, not about
inputs.** The generator tried 96 times per function and recorded every try;
7,353 rows were 5,355 distinct, and twelve functions had exactly ONE input
recorded 82 or 96 times -- `ObjFieldA`, `ObjFlagBit0`, `Field53C`,
`CommMean32` and friends. The cause is the scratch: one deterministic pattern
for every vector, so a function whose only variation is behind a POINTER gets
the same call every time unless angr supplies the bytes.

`MIN_VECTORS` exists because "one vector cannot distinguish a reconstruction
from a coincidence" and it had been counting copies. Measured, not reasoned --
`ColourDistance` at "71 vectors" and 100% instruction coverage passed with
`d1 * d1` replaced by `d1`, and failed at once with the body replaced by
`return 0`, so the harness worked and the inputs did not.

The cure is a SALT, one uint32 per vector: the pattern is
`((i*7+13) ^ (i>>11) ^ (salt*37)) & 0xFF`, the salt is the try index, and
`tests/selftest.cpp` recomputes the buffer from it -- so the bytes are never
stored. It is applied only where it can be OBSERVED, since a function with no
pointer argument cannot read the scratch and varying it there would put the
duplicate count straight back.

6,852 vectors, every one distinct, and the `d1` mutation now fails on ten of
them. Coverage rose with it because the inputs are real: `ClipRect` went from
ZERO vectors to 12, `PointInRect` from 43.8% to 56.2%.

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

**Where the input space is SMALL, enumerate it and stop sampling.**
`MovieBuildName` turns a short movie name into a filename, and everything it
can do is decided by four literal comparisons and two globals. That is 64
cases, so `tools/moviecheck.py` runs the original under Unicorn over all of
them and compares against the rule the reconstruction implements -- an exact
oracle in a tenth of a second, where `tools/vectors.py` could give none at all
(its arguments are strings and it reads two globals the game writes).

Mutation-checked in all three directions, because a check that cannot fail has
not passed: dropping `portal` from the exempt set fails on exactly ONE case,
which is the right number and is the evidence that the corpus reaches each
exempt name in the one flag setting that distinguishes it; flipping the flag
sense fails 24; dropping the slow-machine term fails 12.

`tools/posecheck.py` is the second of the shape and a better example, because
the thing it checks is a 43-byte TABLE transcribed by hand. `WeaponPoseIndex`
answers from an object class (0, 1 or 2), one bit of a field, and that table;
282 cases cover it exhaustively, and no A/B can -- a Boot Camp mission issues
a handful of weapon codes, so the other forty entries are verified by this or
by nothing.

Its mutation counts are the useful part. Corrupting ONE table byte fails 2
cases, swapping the two "kneel armed" constants fails 5, and dropping the
armed branch of one arm fails 8 -- and 5 and 8 are exactly how many codes
select those arms. A mutation whose failure count matches the table is
evidence the corpus reaches every arm, which no amount of "0 disagree" is.

What moviecheck does not reach: the buffer overrun. `dst` is unbounded and the fourth
call site passes a name a mission's script wrote, so a long enough name in a
script smashes a 0x40-byte frame. That is the original's behaviour and is kept
-- but nothing here tests it, and a corpus of short names could not.

**`tools/shakecheck.py` is the third, and it found a defect in the function
BELOW the one it was written for.** `ShakeAt` turns a blast's position and
strength into one of four screen-shake presets, and no drive reaches it: a
probe on a live MAP 01 mission, past both dialogs and thirty seconds in, reads
`ShakeAt=0` and `StartShake=0`, because nothing exploded near enough to the
view. So the tool is that function's verification or there is none.

It seeds the four view-rect globals, calls the original, and reads back the
four shake globals -- 11,088 cases over three view rectangles, eighteen
distances and fourteen strengths.

**The SEED is the whole finding.** The first version started every case from an
all-zero shake state and passed, and `StartShake`'s reconstruction had its four
maxima written as NESTED early returns under a confident comment saying that
reading them as four independent maxima "would be wrong for every case but the
strongest". Each `jle` in the original jumps only past its OWN store. From zero
the two readings agree exactly, because every preset field is positive and
nothing is ever refused -- so the bug was invisible to the tool, to every A/B,
and to the counters, which read 0 for `StartShake` on every configuration.
Seeding a shake already in progress fails 831 cases and names the field.

A second seed with NEGATIVE steps was needed for the same reason one step
further in: every preset step is positive, so comparing the steps signed
instead of by absolute value passes without it. The sign flips in play, as
`ADDR_SHAKE_STEP_X`'s own note says, so that state is reachable.

**And one constant is provably unobservable rather than merely uncovered.**
`AM2_SHAKE_FALLOFF` is 1/512 and `AM2_SHAKE_FAR - AM2_SHAKE_NEAR` is
832 - 320 = 512, so the ramp meets the flat top at exactly 1.0 and the
piecewise falloff is CONTINUOUS. Moving the near radius by one, or swapping the
boundary's `>` for `>=`, cannot be detected by any corpus -- the two arms
compute the same number there. Moving it to 200 does fail, on 192 cases, and
only after offsets in the 250..300 band were added. Say which of a tool's gaps
are gaps and which are theorems.

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

**AND A `sed` THAT DOES NOT MATCH THE INDENTATION IS A MUTATION THAT NEVER
LANDED.** `tools/explcheck.py` was checked in four directions and the fourth --
swapping the two depth keys of the 0x85/0x86 pair -- came back "all identical".
The edit had matched nothing: the anchor was written with twelve spaces and the
line has eight. Grepped for the replacement, saw it was absent, redid it, and
the mutation fails 8 cases. This file already records the same thing happening
to a `case` column in the action-parser probe; it is the second instance, and
the cheap defence is to `grep` for the MUTATED text before believing the run,
not to eyeball the `sed`.

**AND WHEN IT CANNOT, THE SYMPTOM MAY BE A SMALLER CORPUS RATHER THAN A
PASS.** `tools/collectcheck.py`'s classifier both selects the arm and filters
which rectangle pairs are valid, so mutating it did not fail the run -- it ran
27 of 81 cases and reported them all identical. The case count is a failure
condition there now. A mutation that shrinks the corpus reads exactly like a
mutation that was absorbed; check how many cases ran, not only whether they
agreed.

**A UNICORN CODE HOOK FIRES BEFORE THE INSTRUCTION EXECUTES, so the stand-in
address an import is redirected to has to be MAPPED.** Every one of
`collectcheck`'s 81 cases faulted until the page existed -- the fetch faults
first and the hook never runs. It reads as "the original faulted", which is
what a genuinely bad vector looks like.

**A CORPUS DERIVED FROM THE MODEL UNDER TEST CANNOT FAIL AGAINST IT.**
`tools/boolcheck.py` built its token list from the same two tuples its
`expected()` answers from, so deleting a word from the model deleted it from
the corpus in the same stroke: dropping `"T"` reported "1292 distinct tokens,
all identical" instead of one failure. The vocabulary is a SECOND literal now
and the mutation names the token. Caught only because the mutation was tried --
which is the rule this file already states as "a test that cannot fail has not
passed", one level further in: check that the mutation reached the CORPUS, not
just that it reached the code.

**WHEN CAN AN ORACLE'S CASES BE REPLAYED AGAINST THE C? Exactly when the
function reaches nothing that is still the image's.** `SelectFirePose`'s two
callees are both reconstructed and pure, so `tests/fireposevec.h` hands the
recorded cases to the C and five mutations to `item.cpp` fail. `RegionSolvePair`
calls the ORIGINAL `RegionFindPath` -- 1,168 bytes of A* -- so replaying it
would mean putting a test hook into production code for a call the game never
makes that way, and `tools/regioncheck.py` stays a model-versus-original check
like the seven before it.

That is a principled line and worth stating, because the two look the same from
outside: both enumerate a space, both compare against Unicorn, and only one of
them also proves the transcription. Say which kind a new oracle is.

**A CORPUS DRIVEN FROM THE MODEL'S OWN TABLE CANNOT FAIL, AND IT HAPPENED
AGAIN.** `tools/firepose.py` sweeps the nine poses `SelectFirePose` treats as
BRACED, and the first version looped over the model's own `BRACED` tuple.
Deleting `0x06` from it passed -- because deleting the entry also deleted the
case that would have caught it. The symptom is the one `boolcheck` already
records: the corpus came back three cases SMALLER, 4459 against 4462, and a
count that moves at all under a mutation is the tell.

Swept over `range(0, 0x25)` instead it fails in BOTH directions -- two cases for
removing `0x06` and two for adding `0x07`, with the total pinned at 4531 in all
three runs. Second instance of this exact trap in the project, which is the
argument for the rule being written down rather than remembered: it was written
down, and it still had to be caught by mutating.

**AND ITS OUTPUT IS NOT ITS RETURN VALUE, which a check can miss entirely.**
`SelectFirePose` answers 1 on every path past its refusals and does its work by
WRITING `SIGHTCOUT_OFF_STATE`, so an oracle comparing `eax` would pass with the
whole body deleted. The pose slot is seeded with a sentinel before each call, so
"wrote nothing" -- which two of the eight arms really do -- is distinguishable
from "wrote zero". Ask what a function's output actually IS before comparing
anything.

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

**A byte-returning prototype needed the harness to learn one more thing.**
`Log2Mask` writes `al` and leaves the rest of `eax` holding its own argument --
the recorded answer for `Log2Mask(0)` is `0xFFFFFF00`, which is `dec eax` on
zero followed by `xor al, al`. Comparing all 32 bits tests the register
allocator rather than the function, so the vectors carry a `byte_ret` flag and
mask both ends. Measured rather than reasoned: with the flag off, 90-odd
vectors fail and the low byte agrees on every one of them. `VOID` was already
the same problem one step further on, for functions whose prototype returns
nothing at all.

**A 16-way switch is exactly the case for an explicit argument set.** 96 random
32-bit arguments reached 50.8% of `Log2Mask`, because a random integer is
almost never an exact power of two. Twenty-five values in `ARG_VALUES` -- every
power, both ends of the compare chain above 0x100, and near-misses so the
default arm and the unsigned `dec`/`cmp 0x7F` guard are reached -- put it at
100%.

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
ReadScript (0x00444CD0) -> NextToken (0x0043F450)`.

**`0x00444C40` was never in that chain**, and it sat in this file for several
commits as though it were, under a name -- `ParseLine` -- that is as invented as
`ParseScriptFile` was. `ReadScript` tokenises with `NextToken` directly and
never calls it. A call chain is worth checking with a cross-reference rather
than assuming the obvious middle step exists.

What it actually does comes from its only caller. `0x00417B80` carries
`Cheat!!!`, `I am the Juggernaut!`, `I can fly!` and `Aye aye Captain!`, so the
typed line is a **cheat code** and `0x00444C40` is what runs one. A function
that names itself nowhere can still be identified from the one that calls it.

**AND THAT LAST STEP WAS EXACTLY ONE STEP TOO FAR, which decoding the caller
settled.** `0x00417B80` reaches `0x00444C40` on its NO-MATCH path: the typed
line is compared against all 39 cheat phrases, and what is handed on is
whatever was *not* a cheat. Two arms call it as well, with `trigger greenwins`
and `trigger tanwins` -- which are script statements, not cheats. So it is a
SCRIPT LINE runner, and `orig.h` has had it right as `ADDR_SCRIPT_RUN_LINE`
for some time while this paragraph went on calling it the cheat runner.

The reasoning that produced the error is worth keeping, because it is the
rule's own failure mode. Identifying a function from its caller gave the right
NEIGHBOURHOOD -- a typed line, a console -- and then one inference too many
assigned it the caller's subject. A callee reached from a caller's default arm
is characteristically the GENERAL case, not the special one the caller is
named for. `0x00417B80` is also not its only caller's only call to it: it
calls it in three places.

**And the name `ParseScriptFile` was mine, not the program's.** There is no such
string anywhere in the image; `0x00444CD0` calls itself `ReadScript`, in
"ReadScript: Could not open %s for reading.". The macro said `PARSE_FILE` while
the function said `ReadScript` for the whole of this work, which is exactly the
drift the naming rule exists to stop. Nine more real names were sitting in the
strings unclaimed and are now in `orig.h`: `ScriptResurrectItem`,
`ScriptSetObjBitmap`, `UpdateObjectScript`, `ChangeObjectFrame`,
`SetObjScriptState`, `DefParseInfoFile`, `DefGameParse`, `DefObjParse`,
`DefLinkParse`. Two source filenames come with them -- `script.cpp` and
`objscript.cpp`.

**The whole script parser is reconstructed** -- `ParseScriptFile` and every
function under it, 42 in all, from the file down to the last action operand.
What is still original below it is engine: DirectDraw, comm, bitmap loading,
event dispatch, reached *through* the handlers rather than being parser code.

**Parse the game's own data in the game, not in an emulator.** I was about to
build a Unicorn harness with hooked file I/O to cover the action parser's 59
keywords, of which bootcamp and the campaign reach 24. `AM2_PARSE_ALL=1` makes
our `ReadScript`, after the game's first script load, parse every other script
the game ships and dump each 0x48-byte action record; `AM2_DUMP_ACTIONS=1`
prints them. 104 files, 9,934 records, 48 distinct action codes -- which is
exactly how many action keywords appear in any shipped script, so the sweep
reaches everything reachable. Byte-identical across runs, so it is an exact
oracle. `tests/actions-reference.txt` is the recording and `tools/actdiff.py`
maps a differing record back to its file, line and keyword.

Three things it took: the game chdirs into the map directory before loading, so
the file list must be absolute (`AM2_SCRIPTS`); feeding `EULA.txt` to the
statement dispatcher takes the process down, so the sweep covers `data/` and
`rules/` only; and state accumulates until the fixed tables overflow after
about seventy files, so two runs with the list in opposite orders cover all 104
-- clearing between files is worse, because the arrays and their capacities
have to be cleared together and the names the loaded mission still holds must
not be freed.

**That oracle found nine defects an A/B cannot see.** A mis-parsed action still
produces an action, so the log and the pixels agree either way. Among them:
`playsound` initialises two fields to zero and I had transcribed a scaled token
index, having read a dump with the `xor eax, eax` filtered out; `order`'s
`follow` and `goto` were swapped; `dropitem`, `setobjstate` and `fireweapon`
each put their two names in the opposite fields from how the statement reads;
and the AI modes are attack 6, defend 7, ignore 2, evade 5 -- neither
sequential nor in keyword order. Reading alone got all of them wrong.

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

**AND FOR A TABLE THIS SIZE, GENERATE THE CODE FROM IT RATHER THAN
TRANSCRIBE IT.** `DirtyCollect`'s decision is an 81-entry byte table over
twenty-four arms; the switch I hand-wrote from a printed decode put six codes
in the wrong arm, and no amount of re-reading would have found it. What found
it was diffing my case groupings against the image's table programmatically,
and the fix was to emit the `case` labels from the table. Dumping a table is
the first half of the rule; not retyping it is the second.

**A jump table's order is not the order its arms are laid out in.** State 2's
thirteen sub-state arms are nine of one shape -- repaint if the overlay is
dirty, then `DrawMenuOverlay` -- differing only in which painter they call, so
they compress to a table. Reading the bodies top to bottom and numbering as you
go gets four of them wrong, because the linker emitted them 27, 28, 26, 29, 30,
31, 25. Take the order from the jump table at `0x00426230`, never from the
addresses.

That table also confirms what this file worked out by probing: arm 34 is index
12 and calls `0x00425DA0`, the in-mission ESCAPE handler, and ordinary play
sits in 33. Two independent routes to the same fact.

**TWO NEARLY IDENTICAL LOOPS ARE WORTH DIFFING BYTE FOR BYTE.**
`VehicleBlockWeight` walks the vehicle mask twice, once for the boat and once
for everything else, and the two bodies are the same to the eye. They are not
the same to `cmp`: one twenty-six-byte run differs in exactly ONE byte,
`8d 44 24 28` against `8d 44 24 24`, which is the boat sampling the objects at
its OWN point where every other kind samples at the mask point. Four other
differences sit around it -- a different weight helper, a different damage
threshold, a different empty-mask exit, and no sound at all -- so merging the
two into one parameterised loop would have lost five things and looked tidier
for it.

Reading them side by side is not enough; `img.read` on both and comparing the
hex is, and it takes one command. Where the original repeats itself, diff the
BYTES before believing the repetition.

**And a fourth: A TABLE WITH NINETEEN INDICES AND TWO ARMS.** `TrooperFire`'s
switch at `0x0044A360` covers codes 0x14..0x26 through a byte table whose
entries are only 0 and 1, so it is a FILTER rather than a dispatch -- seven
item kinds leave the trooper's state alone and everything else, including
every code outside the range, ends it. Reading the arms finds two behaviours
and no idea which code takes which; the byte table is the whole answer, and it
is four lines of Python to decode.

**The ITEM CAPTIONS are the program's own names for those numbers**, and
decoding them is what turned two of these tables into sentences. `0x00419A30`
is a 25-entry jump table reached through the 41-byte index at `0x00419A94`, and
each arm pushes one four-letter string: GREN, FLAM, BAZ, MORT, HvMG, RIFLE,
AUTO, MINE, EXPL, FLAG, MSWP, MEDI, AIRS, PARA, RECO, NOTE, FLAK, VULC, SNIP,
DISG, MAG, AERO, WREN, M80. With those, `ObjCodeUnmapped`'s five zero entries
stop being numbers: AIRS, PARA, RECO, MAG and AERO, the items that do NOT turn
the soldier to face what he is aiming at. **When a switch is on an item or
object kind, find the table that gives those kinds names before reading the
arms.**

**A SIXTEEN-ENTRY TABLE CAN BE MOSTLY IMPOSSIBLE, and that is worth knowing
before writing any of it.** `AddSightBlocker` builds a code from four
comparisons -- (left < x), (top < y), (right < x), (bottom < y) -- and jumps
through a sixteen-entry table. EIGHT of the sixteen share the refusal exit:
seven cannot happen at all with `left <= right` and `top <= bottom`, and the
eighth is the viewer standing inside the box. So the table has eight real arms
and eight that exist because the compiler needs a dense index, and reading the
arms in order without decoding the table would invent behaviour for codes that
never arrive.

Generated from the image, not transcribed -- the decision `DirtyCollect`'s
eighty-one arms forced, and the reason is the same: a hand-written mapping of
sixteen codes onto eight bodies is exactly where six of eighty-one went wrong
before.

**Three instances now, and the second failure mode is SLOTS SHARING AN ARM.**
`WeaponClassOf` (`0x0042AAE0`) lays four arms out in one order and dispatches
them in another -- kinds 2, 3, 4, 5 answer 2, 3, 1, 4, where reading the bodies
top to bottom gives 1, 2, 3, 4. `SpriteKeyForKind` (`0x0043A5F0`) has eight
table slots and only SIX distinct targets, because selectors 0, 1 and 2 all
point at the same code; counting the bodies gives six arms for an eight-case
switch. So the table answers two questions the bodies cannot -- which arm each
index takes, and how many indices share one. Read it every time.

**A THIRD failure mode: an arm that ENDS INSIDE ANOTHER.** `UnitKindMatches`
kind 3 does two tests and then `jmp`s into kind 4's fourth test to share its
epilogue, so the `n*10+12` code sits in kind 4's body and kind 3 never reaches
it -- reading the bodies top to bottom gives kind 3 four candidates when it
has three. `PlacementAllowed` arm 16 does the same in the other direction: it
pushes its three arguments and jumps into the middle of arm 15 to make the
call. Two in one session. The jump table does not show this at all; only
following every branch out of an arm does.

**And a fourth, which is not about the table: AN ARM CAN BE UNREADABLE FROM
ITS OWN BODY.** `PlacementAllowed`'s five vehicle arms pass MaskBlockWeight
kinds 1, 0, 2, 3, 5 in table order, which is the WeaponClassOf shape exactly
-- and is not that at all. Each arm passes ITS OWN `ADDR_UNIT_TYPES` record's
+0x08 field, and the apparent scramble is the table's, not the switch's. One
`am2.Image.read` of eighteen records distinguished the two readings; no amount
of staring at the arms would have. **When a switch is indexed by a table the
image ships, DUMP THE TABLE before writing anything down about the order.**

That same dump settled three other things for free: why `SpriteKeyForKind`
and `UnitKindMatches` share arm zero three ways (riflepill, bazookapill and
mgpill are one sprite set), which record each building arm belongs to, and
that `ADDR_TILE_KIND`'s per-tile byte is `(army + 1) * 0x10`.

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

**A HOT function can still be undiscriminated, and `ShotStrike` is the sharp
example.** It runs **13,582 times** in a driven Boot Camp mission with eight
rounds of firing -- the busiest thing reconstructed in weeks -- and `combat`
still cannot see a change to it. Making a non-explosive shot penetrate instead
of stopping at the first thing it damages left the run clean: 21 identical
messages, frames in step at 15796/15611, pixels in the usual meaningless band.

The reason is the shape of the call count, not the size of it. Of those 13,582
calls almost all find nothing at the point and fall straight through to the
terrain test; the branch the mutation moves is behind ApplyShotDamage, and
this file already records that eight rounds of `combat` take `DamageObject` to
SIX. Six shots landing differently is invisible to a configuration whose pixel
budget is disabled by construction and whose log never mentions a shot.

**So ask which BRANCH the calls take, not how many there are.** A five-figure
counter reads like thorough coverage and here it certifies only the path that
does nothing. What would settle it is `tools/objdump.py` on a target's health
across the two sides, which is the class of check this project already keeps
for functions that write an object field and return nothing.

**It is not only the five-figure counters.** `NearestClearPoint` runs EIGHT
times on the same drive -- live, in the trooper band, placing something at a
passable point -- and returning the start point unchanged without spiralling
at all leaves `bootcamp` at its floor of 22 and `mission`'s log and 16-node
tree identical. So the standing is the same at eight calls as at 13,582:
covered and unchecked. Two mutations in two commits, both uncaught, is the
suite telling you where its blind spot is -- anything whose only effect is
WHERE something ends up on a map the pixel checks cannot compare.

**AND THE OTHER HALF IS NOW MEASURED CATEGORICALLY, not inferred.** The two
mutations above each left one thing alone, so "uncaught" could always have
meant "that branch never ran". `MoveStepPoint` settles it: it runs **175,145
times** on a driven Boot Camp mission and computes where every moving object
goes next, and adding a flat 50 to EVERY step -- a mutation that cannot fail
to matter, on the hottest function in the tree -- leaves `combat` clean, with
21 identical log messages and frames in step at 16521/16587.

So the suite does not fail to notice particular movement bugs; it cannot see
movement AT ALL. Every configuration that reaches live play has its pixel
check disabled by construction, because two unsynchronised runs of a scrolling
mission differ by a quarter of the frame either way, and nothing else it
compares mentions a position. That is a fact about the harness worth knowing
before crediting any A/B with covering a mover.

**Half of that blind spot is closed now, and the half that is not is worth
stating.** `tools/objdump.py --table` dumps every registered object as VALUES
-- type, flags, army, position, tile, both box rectangles, health, cell count,
and never a heap pointer -- and `ab.sh bootcamp` takes it as a `state`
artifact and diffs it exactly, no budget. 1,609 objects in 0.6 s, and two
independent runs give a byte-identical dump, which is what makes an exact diff
legitimate rather than hopeful.

It is taken AT THE BRIEFING and that is the whole design: the game composes no
frames while a dialog is up, so every object is exactly where the LOAD path
put it and nothing has moved. Mutation-checked -- writing a scenario row's x
into its y moves uid 3e8 from `pos=1743,1052` to `pos=1759,1743` with its hit
rectangle following, and the diff names the object and the field, which
"154,855 pixels differ" never does.

**So it covers construction and NOT gameplay.** The two mutations that started
this both happen after the briefing, and neither would be caught by this
either -- a live table moves, so it cannot be diffed exactly. What is now
checked is everything the map load builds: positions, boxes, ownership, health
and cell membership counts for all 1,609. What is still verified by pixels
alone is what happens to them once the mission starts.

**`combat`'s pixel figure is BIMODAL, and both modes are meaningless.** Four
runs of one build gave 177,112, 716, 177,109 and 684 -- two clusters, three
pixels apart within each. That is not noise around a mean; it is whether the
two sides' camera happened to be at the same point in the scroll when the
shot landed. Its budget is disabled for the same reason `mission`'s is, so
`ab.sh` says "A/B clean" at 22.5% of the frame -- and the log is the evidence
there, exactly as for `mission` and `intro`. Do not read a `combat` pixel
count as a result in either direction.

**`mission`'s FRAME RUNAWAY IS ONE-SIDED, AND THAT IS STRUCTURAL RATHER THAN
RANDOM.** Every time it has fired, it is the ORIGINAL half that runs away --
25,932 against 7,806, 25,917 against 8,135, 25,738 against 8,057 -- and never
ours. That direction is what the configuration makes: the `AM2_NOPATCH=1` half
runs the image's own code with no reconstruction and no trace stubs in front of
it, so it is simply faster, and under load the gap widens until the 300% gate
trips. Ours staying in its 6,300-8,300 band while the other side climbs is the
signature of the machine, not of a change.

So read the SIDE before the ratio, every time; the named `.volatile` artifacts
carry it. And the cheap question still beats both: **ask whether the new code
executes at all.** For the air functions the answer is a dump -- the whole
`ADDR_AIR_SAVE_BLOCK` header reads all zeros through a live Boot Camp mission,
so nothing in that subsystem runs and no reconstruction of it can move a frame.
That took one probe and settled what five A/B runs could not.

**IT HAPPENED AGAIN, IN REVERSE, AND THE CONTROL WAS THE ONE SAMPLE THAT
LIED.** `bootcamp` had read 22 pixels on every run of a long session. A batch
landed and it read 76, twice, and once more with the new function mutated to
return immediately -- so the body was not the cause and the diff had nothing
else in it that could execute. One run of the PARENT commit came back at 22,
which reads as decisive: same machine, minutes apart, one variable.

Two further parent runs both read 76. The parent had moved, not the change.
What moved with it was the machine: `uptime` had gone from 0.62 to 7.45 over
the session, and CLAUDE.md's own note below says an unequal starve is exactly
what that does to two halves of a run.

So the rule below is symmetric and the second half is easy to forget: **give
the control as many samples as the thing it is controlling for, in BOTH
directions.** One clean control does not establish a regression, and one dirty
control does not establish one either. The cheap question that settled it
first was whether the code executes at all -- the pass count reads 0 on that
screen and the function returns immediately -- and that should have been
believed rather than re-litigated with screenshots.

**A single clean CONTROL run does not establish a regression, and this cost
an hour.** Two `ab.sh` runs of one build failed the frame gate on all three
configurations with identical logs; the parent commit, run once on the same
machine minutes later, came back clean. That reads as decisive -- the change
is the only difference -- and it is not: the control is one sample of a thing
that fails about one run in three under load. The same build then ran clean,
and a behavioural difference does not come and go.

What settled it was neither run. The function under suspicion has a COUNTER
of ZERO on that drive, so it cannot have changed a frame count. **Ask whether
the new code executes at all before comparing runs of it** -- that question is
cheap, deterministic, and answers "could this possibly be the cause" outright.
Give the control as many samples as the thing it is controlling for, or find
a measurement that does not need samples.

**Re-run an A/B difference before believing it.** One `bootcamp` run reported
64,391 differing pixels and "the frame is wrong"; it was whole-frame palette
shifts of one to five per channel, and three further runs of the same build
gave the usual 22. The failing run had been started seconds after Xvfb itself.
`ab.sh` already says to read a difference before believing it -- re-running is
part of reading it.

**But re-running proves nothing once the machine's own behaviour has moved,
and the control is the PARENT COMMIT.** Under an external load average of
18-21 -- other users' processes, nothing of ours running at all -- the two
halves of a run are starved unequally. Measured in one session: frame counts
fell from the usual 6,000-13,000 to 1,700 and then to 64, `combat` went
out-of-phase five times running, and `bootcamp`, which had read **22 pixels on
every run that day**, read 291,505 with an IDENTICAL log.

Re-rolling under that only samples the new distribution. What settles it is to
stash the work, build the commit before it, and run the same configuration:
the parent failed the same way, 306,172 pixels and log differences, on code
that had been clean hours earlier. So the noise was the machine.

**Do not pipe `ab.sh` through `tail -N`.** A short tail drops whole
configurations: a `bootcamp mission` run tailed at 12 lines keeps only
`mission`, and the run's own output file then has no record that `bootcamp`
happened at all. That is how a commit message came to assert "bootcamp clean at
its usual 22 pixels" for a figure nobody had seen -- the artifacts could still
show the log and the object table identical, but the pixel count exists only in
the output that was thrown away. Capture the whole thing and read the part you
need.

Check `uptime` before believing an A/B failure, and reach for the parent
commit rather than a fourth re-run. **The `frames` line is the early warning**
-- it is printed before the pixels and a collapse in it means neither side ran
the scene the other did.

**And the `frames` line is `orig/recon`, in that order -- read WHICH SIDE
moved before reading the ratio.** A run of `mission` failed the gate at
22372/7957 with a function mutated to answer nothing, and it was written up as
the reconstruction running away and therefore as the mutation being caught.
The first number is the ORIGINAL's, which `AM2_NOPATCH` runs and no
reconstruction can reach, so it was evidence of nothing; the next unmutated
run on the same machine gave 25493/8082, the same shape with nothing mutated
at all. The mutation had in fact moved nothing the suite watches -- identical
log, identical widget tree, and a reconstruction-side frame count inside the
run-to-run spread -- so a function that had just been recorded as CHECKED was
merely covered.

That is the rule one entry above, applied to the other half of the comparison,
and it was got backwards within a day of writing it down. The cure is not to
remember the argument order: it is to read the two artifacts, which are named
-- `$WORK/<cfg>-orig.volatile` and `$WORK/<cfg>-recon.volatile` hold the two
numbers with the side in the filename.

**AND THAT BAND WENT STALE, WHICH TURNS THE CHEAPEST CHECK INTO A FALSE
ALARM.** The paragraph below quotes ours as 6,291-8,300 on `mission` and says
one number you already know beats a re-run. Measured today across three runs
-- 17:41, 06:25 and 06:21 -- our side read **533, 652 and 625**, an order of
magnitude below it, while the original's read 25,563, 26,305 and 26,433,
squarely inside ITS documented band. Two of the three predate the session's
work entirely, so nothing in the reconstruction moved it.

So `mission` now fails its frame gate on every run, and the failure is not
evidence of anything. What the run still says is the part that was always the
real evidence: log identical at 13 messages, object state identical, widget
tree identical at 16 nodes. Read those and ignore the gate on this
configuration until someone finds out what changed.

Why our side dropped from ~7,000 to ~600 is NOT established here, and the
honest thing is to say so rather than guess -- three samples say it is stable
and old, not that they explain it.

The lesson generalises past this number. A remembered band is a measurement
with no timestamp, and this file offers several as shortcuts. When one
disagrees with a run, check whether the BAND moved before concluding the
BUILD did -- the artifacts are on disk and carry their own dates, which is
how three runs across eleven hours were compared in one command.

**Know the reconstruction side's NORMAL BAND, which is the cheapest form of
this check.** Over one session `mission` failed that gate on four separate
builds and the original's side was the runaway every time -- 22372, 25493,
22771 -- while ours read 7957, 8082, 7053 against a clean run's 6291 and 7600.
Our side never left its band, so no build in that session could have caused
any of it. One number you already know beats a re-run, a control commit and a
ratio.

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

**ONE PREDICATE ASKED TWICE CAN BE USED IN OPPOSITE SENSES, and writing both
the same way is a defect no pixel can see.** `CreateTrooper` calls
`ObjIsFriendly` at its head and again at its tail: the first is `je` past an
OR, so `OBJ_FLAG_REVEALED` goes on when the answer is NON-zero, and the second
is `jne` past a call, so `ObjConceal` runs when it is ZERO. Both were written
as the negation and half of that was wrong. `bootcamp`'s object-state dump
caught it -- eight lines differing by exactly 0x800, with the log identical and
the pixels at their usual 22 -- which is the fifth time that artifact has found
something the verdict line would have called clean. Two calls to one predicate
are two branches to read, not one.

**Grep for how the tree already reaches a GLOBAL before writing an expression
for it.** The naming rule -- grep the address before inventing a name -- has an
exact analogue for ACCESS, and ignoring it cost three defects in one function.
`EnsureSpriteAaiRecord` indexed `ADDR_AAI_RECORDS` and `ADDR_RECORD_LISTS` as
though the global were the array; both hold a POINTER to it, the original loads
`mov eax,[0x51614c]` and only then indexes, and `objtype.cpp` already spelled it
`(*(void ***)(uintptr_t)ADDR_RECORD_LISTS)[n]` in three places. A fourth
spelling went in without the grep. The one global in that batch reached through
an existing macro -- `kScriptNames` -- was right first time.

The payoff is not tidiness, it is that DIVERGENCE BECOMES VISIBLE. `ObjAttachTo`
gates two arms on `ADDR_MP_SESSION` and I had written the comm object; what
exposed it was that `ObjsAreAllied`, four hundred lines up in the same file, is
the SAME inlined block and gates them on `mp`. Had the surrounding lines been
written in a private idiom there would have been nothing to line up against --
and no A/B could have caught it, because `ADDR_MP_SESSION` is 0 on every drive
this project has, so both arms are unreachable and the run comes back clean.

**An exit NOTED is not an exit reproduced.** `ObjInitCommon`'s header comment
said "both exits set eax deliberately" and the body was then written as two
independent `if`s with a shared tail. The original's no-cells case is an EARLY
RETURN that also skips `ItemLinkCells`, so every object without a cell list was
linked into the map twice; both configurations truncated at the same point in
map load, 37% of pixels differing. Write the exits first, from the epilogues,
and count them -- 4 `ret` instructions can be 5 return PATHS sharing one, which
is what `ObjAttachTo` does.

**Verifying an ADDRESSING MODE is not verifying INDIRECTION DEPTH.** The same
function's record loop was "verified against raw bytes" -- `8b 46 0c` / `8b 00`,
no SIB, no index -- and that reading was correct and beside the point. It
established there was no index; it said nothing about the chain being
`array -> records[0] -> sprite -> key`, three loads where I had written two. A
byte-level check of one instruction cannot answer a question about a sequence of
them. CLAUDE.md already warns that `obj -> table -> slot` needs two
dereferences; this is the same trap one level along.

**Three defects in one session, and NOT ONE was a wrong offset.** Every one was
control flow or indirection depth, and `tools/checkoffsetuse.py` reported
byte-identically before and after all three fixes -- correctly, since it compares
which offsets are NAMED. Do not read a clean offset check as a clean function,
and do not stack a second unverified unit on an unverified one: both bugs
produced the identical failure signature, and telling them apart depended on
only one being in flight.

## THE OFFSET-NAMING MISTAKE HAS FOUR SHAPES, and only one is in this file

This file already warns at length about naming a field pointer as a table
base. One session produced four distinct forms of the same underlying error,
and only the first is the one described:

| shape | instance |
|---|---|
| an offset that indexes correctly from the WRONG BASE | `ADDR_SPRITE_GROUPS` -- and BOTH its walkers were wrong, at +4 and +8, with the arithmetic tiling to ten records from either |
| a base correction APPLIED WHERE NONE WAS DUE | sprite pair 3's loader points at its record base; carrying pair 1's fix over would have put that table four bytes early |
| a name belonging to ANOTHER RECORD at the same displacement | five invented `TURNPLAN_OFF_*` over the existing `SIGHTCOUT_OFF_*`, which `checkoffsets` cannot see because a new prefix has nothing to compare against |
| one offset carrying names for SEVERAL TYPES | `0x52C` has three (`VEHICLE_OFF_KIND`, `OBJ_OFF_TABLE_REC_KIND`, `SAVED_OFF_TABLE_REC2`); `0x548` has two, one of them a record SIZE rather than a field |

All four compile. All four are invisible to `checkoffsets`. None is caught by
an A/B, because each indexes *something* correctly.

**The question that catches all four is "which record is this, HERE" -- asked
before the name is written, not after.** Its answers are: what is BEFORE the
field (a negative displacement in this body, or its absence); which base this
caller passes; what other prefixes hold that offset; and what type the object
actually is at that instruction.

Where it cannot be answered, a RAW OFFSET with the reason recorded beats
either candidate name. `orig.h` already keeps a dozen `FIELD_` placeholders on
that principle; this extends it to offsets that have too MANY names rather
than none.

## ARITY AND IDENTITY ARE DIFFERENT QUESTIONS, and answering one feels like both

`Step3TurnBlocked`'s arguments went in reversed after a reading that was
correct at every step. `tools/espmap.py` gave the frame slots; tracing which
register takes which slot was right; reading which offsets each register
touches was right, and they separated cleanly into object fields and record
fields. **None of that says which CALLER VALUE lands in which slot.**

I had read the call site -- to count the pushes, because the `add esp` was
lying about arity as usual. Having done that, checking it again for argument
IDENTITY felt redundant. It is not: the pushes answer "how many", and only
knowing what the caller's registers HOLD answers "which".

The result is the ADDR_ENTER_VEHICLE shape exactly -- internally consistent,
every field access on the other record, nothing wrong from inside, and it
compiles. Here it also could not be caught by running: both configurations
passed with it installed, because the function is cold.

So the rule below is two rules. Read the call site for the COUNT, and read
what the caller put in each slot for the ORDER, and record which instruction
settled each.

## Take every signature in a family from its CALL SITES, before writing any of it

Fifteen functions implementing five roles three times -- two hardcoded row
pools and one over a SEQ context -- and reading the five generic BODIES gave
two wrong signatures out of five. Both would have compiled.

- `SeqCtxInit` takes **five** arguments, not the three its first stores
  suggest. The caller pushes `0x20, 0x20, 0x28, 0xC8, ctx`, and the two extra
  are RowAlloc's width and height, which the hardcoded versions inline.
- The generic release takes the **context first**, not the record. Read off
  the body it looks the other way round, because the prologue's own pushes
  shift every `[esp+N]` and I had not counted them.

**A body shows the arguments a function USES; only the caller shows how many
there are and in what order.** And `ret N` does not help here -- all of these
are `__cdecl`, so the epilogue carries no size at all. CLAUDE.md's rule about
comparing `ret N` is about STDCALL and is silent about these; I cited it
confidently one message before checking, which is its own lesson.

`tools/espmap.py` is the tool for the second failure: it normalises every
`[esp+N]` to a frame slot, so an argument read at two different depths is one
slot rather than two. It settled a question three readings had not -- the loop
bound and `capacity` are the same slot -- in one command.

**AND THE SAME SUBSYSTEM CAN CARRY ITS SLACK IN TWO PLACES.** The hardcoded
pools have `capacity + budget` slots and test `count > capacity`; the generic
context has exactly `capacity` records and tests `count > capacity - margin`.
Same protection, opposite implementation, and writing either from the other's
outline runs the array off its end. Where a family is a REWRITE rather than a
re-emission -- 0.222 similarity here against 1.000 between the twins -- assume
nothing carries over but the shape.

## Five things the last function taught, all of them cheap

**AN OFFSET WITH NO PREFIX IS INVISIBLE TO `checkoffsets`, and that is the
hole this file only ever described from the other side.** It already says a
NEW prefix has nothing to compare against. The same is true of a displacement
read straight out of a disassembly -- `[edi + 0xc]`, `[ebx + 0x94]` -- which
arrives attached to no family at all. Two fields were written up as "no reader
before now" when both had been named twelve lines apart in `orig.h` for
months. Grep the BARE NUMBER; the checker cannot.

**DIFF BEFORE DECLINING TO MERGE, not only before merging.** The rule here is
"where the original repeats itself, diff the BYTES before believing the
repetition", and it exists to stop a merge that flattens a real difference. It
is worth exactly as much in reverse: two copies of one loop were declared "not
identical" from reading them, and `difflib` over normalised disassembly showed
48 instructions against 50 with every difference an `edi`/`esi` swap bar the
log line. The refusal to share a helper was as unfounded as a merge would have
been. Thirty seconds settles it either way.

**PRINT THE WHOLE LITERAL POOL AT ONCE.** Four separate errors in one function
came from reading strings out of fixed-size disassembly windows: a string
truncated by the window and reasoned about as a complete phrase for three
commits (`"PULSE Adding to "` is `"PULSE Adding to freelist from sendque
Buffer seq %d  elelment %x"`); a second log line just past the end of a dump,
silently dropped; and two strings that differ only in `%d` against `%x`, which
is why one helper had to take the format string as a parameter. One command
dumps every `push imm32` that points at a C string. **A literal that ends
without punctuation mid-phrase is the tell.**

The house style is what actually caught the truncation: this tree takes format
strings from the image through named `ADDR_STR_` constants rather than
inlining them, so writing the call FORCED the real text to be fetched. A
convention that makes a class of mistake impossible to write down beats one
that documents it.

**WHICH MODULE A FUNCTION GOES IN IS DECIDED BY THE LINK, NOT ONLY BY
`checksplit`.** That tool asks whether a file NAMES a Win32 or COM type. A
function can name none and still belong elsewhere: four new functions went
into `air.cpp` because that is the original's translation unit, and `air.cpp`
is in `SELFTEST_SRC`, so calling `MsgListCopyByKey`, `CommSend` and
`DumpMsgList` -- all in `win32/dplay.cpp` -- broke a link that has no DirectX.
`make` was clean; only `make check`'s link guard failed. `commmsg.cpp` was the
right home and said so itself, carrying a forward declaration of `CommSend`
with a comment explaining the very constraint I had re-created one file over.

**ANCHOR A SCRIPTED INSTALL EDIT ON THE LAST `patch_replace`, NOT ON THE
CLOSING BRACE.** An install function ends `return 0; }`, so an edit anchored
on the brace lands AFTER the return -- the dead-patch defect this file records
from `dist_install` and `savetag_install`, where four reconstructions were
never installed and every tool read them as done. `checkpatches.py` was
written after that incident and this was the first time it caught the thing it
was written for. The lesson recorded then was to anchor on something every
install function has; a closing brace qualifies and was still not enough,
because what matters is what comes immediately BEFORE it.

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

**The scenario table is another global worth dumping, and the dump is exact.**
`ParseScenarioPart` fills a structure nothing draws: four parts per scenario,
each a malloc'd array of 0x6C-byte rows with a kind, a point, three rewritten
bytes and a name. A wrong field there changes which units a mission starts
with, which reaches the screen only as a different-looking map. Reading the
table out of the running game over the control socket and diffing against
`AM2_NOPATCH=1` compares it byte for byte with no budget -- the same idea as
`tools/trigdump.py` and `ctl widgets`.

Boot Camp's table is ONE row -- `05800000 cf061c04 ... greensarge1`, kind
0x8005 at the world point (0x6CF, 0x41C) -- so say what one row reaches.
Storing the negated flag straight fails it (`000001` against `000101`), and so
does dropping the default that forces the third byte to 1 (`000001` against
`000000`), which between them prove that BOTH rewrite rules are exercised and
that the wire bytes behind them are 1 and 0 respectively. Dropping the name's
length from the byte total kills the process outright before the map finishes
loading, which is caught in the bluntest possible way. What one row cannot
reach is the empty-name arm, a part with no rows, and the three
variable-length runs the parser skips before it reads anything it keeps.

**A PARTIAL offline oracle is often available where a whole-function one is
not, and it does not have to become a tool.** `ItemSetBox` is cold -- 0 calls
on Boot Camp with walking, firing AND turning, while the sibling it calls
reads 1,450 on the same run -- and it cannot be emulated whole: it unlinks
from the map, reallocs, and relinks. But everything up to the grow test can
be, because `ItemPreDestroy` returns at once on an object whose cell count is
zero, which a scratch buffer's is. Emulating entry to `0x00429BF1` and
comparing both the eight rectangle stores and the count in EAX gives 24,696
cases and 0 disagreements over spans from -4096 to 30000, including every
+/-2 and +/-256 boundary.

That was run once and NOT committed as a check, unlike `formationcheck.py`,
and the difference is worth stating: the arithmetic it covers is `RowAlloc`'s,
which runs 2,512 times a mission, so the subsystem has live coverage and only
this transcription of it does not. A tool earns `make check` when nothing else
watches the thing at all. Mutation-checked all the same -- dropping the -2
fails 2,236 cases, +2 becoming +1 fails 22,932, transposing the position into
the rectangle fails all 24,696 -- because a check that cannot fail has not
passed whether it is committed or not.

What stays verified by reading is what needs the game: the two guards, the
realloc, the entry initialisation and the relink.

**`tools/formationcheck.py` is the third enumerating oracle, and it exists
because the subsystem is COLD.** `FormationPoint`'s counter is 0 on a full
Boot Camp run and on the campaign: both drivable missions start with a squad
of ONE, so nothing places a follower. Past slot 11 the twelve-entry slot table
runs out and `FormationPointFar` computes the position instead -- sixteen
slots to a ring, the distance stepping 32 per ring, doubled for a vehicle, and
a swing away from the front of one. That is decided by three inputs, so it is
3,520 cases and it enumerates in 0.28 s.

**Stop the emulation where the answer is final, not where the function
returns.** It runs the original from its entry to `0x00404354` and reads the
facing out of `BL` and the distance out of `EDI` -- the instruction after the
last write to either and before the x87 step. Past that point it calls Cos8,
Sin8 and a settle that dispatches through a global function pointer, none of
which is set up outside the game, so a whole-function emulation is impossible
and a partial one is easy. The trig, the clamp and the settle are shared
verbatim with `FormationPoint` and stay verified by reading; say which half a
partial oracle covers.

**A model of a callee is a second source of truth, and mine was wrong.** The
first version modelled `AngleDelta`'s wrap by hand as
`((a - b + 128) & 0xFF) - 128` and reported 256 mismatches, all of them in the
type-3 swing and all of them the model's. Calling the image's own
`0x0042DD90` instead took it to zero. The reconstruction under test uses our
`AngleDelta`, which the selftest already checks against that address, so this
keeps one source of truth rather than adding a third.

Mutation-checked in four directions and the counts are the useful part:
halving the ring size fails 2,760 of 3,520, doubling the step fails all 3,520,
and both the extra 0x20 and the sign of the swing fail exactly 304 -- which is
how many type-3 cases take that arm.

**`tools/anicheck.py` reads inside a structure no A/B can see.**
`LoadAnimTable` is the tail of a `.ani` load — the list of animations over the
sprites `LoadSpriteSet` has just read — and nothing about it reaches the log.
A wrong field draws the wrong sprite, but `bootcamp` screenshots the briefing,
where no soldier is on screen, and `mission`'s pixel check is off by
construction. So `AM2_DUMP_ANIMS=1` prints what the game built and the tool
parses the twenty shipped `.ani` files itself and compares entry by entry.

**A file format that consumes its input exactly is its own proof.** Parsing all
twenty with the layout taken off the disassembly ends each one on its last
byte — 349 animation entries, 1,103,262 of 1,103,262 for `rifleman.ani`. A
mis-sized field could not do that, which is better evidence than reading the
loader twice.

Say what it does not reach, as always. All 121 borrowed entries in a Boot Camp
run resolve to the predicted pointer, which covers the fallback search and the
final fixup — `explosions.ani` passes no fallback at all, so its three can only
come from the fixup. But no borrowed id is missing from `rifleman.ani`, so the
`entries[0]` last resort never fires, and rifleman's 52 ids are all distinct,
so "the last match wins" and "the first match wins" cannot be told apart. Both
stay verified by reading. Tested in the failing direction: dropping the
`next == 0 → -2` rewrite fails 6 of the 21 tables and names the field.

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

**And one of them can be ABSENT, not merely out of order, which a sorted set
does not fix.** One `quit` run came back with `Receive thread got event 0` on
the original side and not on ours; three re-runs were clean, and the line is
the receive thread's own, which no reconstruction in that run touched. So the
thread sometimes exits before it logs. Sorting makes the ORDER stable and
leaves the COUNT racing, so this configuration can still fail for a reason that
is not a defect -- re-run it before believing a one-line difference here, the
same as for a windowed pixel figure.

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

**`tools/ab.sh multi` drives that path now**, through to ENTER BATTLE NAME,
and types into the field. 7 identical messages and 0 pixels, three runs. It is
the only configuration that reaches the EDIT BOX at all — the CONTROLS
dialog's key-capture boxes look like text fields and are a different class,
where all five `Edit*` counters read 0.

**Its pixel check does not discriminate, and that is measured.** Making
`EditTakeFocus` skip installing `g_charHandler`, so nothing typed ever
appears, moves **72** pixels — "Zulu" in a menu font is small — which is under
any budget that survives a blinking caret. So it covers the path and gross
errors, and the handler is checked by driving and looking at the field.
**Measure the defect signal, not just the noise floor**: three clean runs said
the noise was 0, which was true and useless. What decides whether a budget is
worth anything is how big a real error is.

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
| windowed, `-w` | identical, 6 messages | 2–10 (was **0** while it stayed black) |
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

**The windowed frame was called static and pixel-perfect, and it is neither
any more.** That claim was made when the client area never got painted at all:
a black rectangle compared against a black rectangle is exactly zero, and
reads as the strongest result in the table. Wine hands this prefix a lockable
primary now, so the area draws — and four shots two seconds apart differ by 4
pixels each in one 10×10 box at (325,232), so something in it blinks. With
both sides painted the two frames differ by 2 to 10.

The budget is 50 for that reason and no longer 0. A result in the 195,000
range still fails loudly and means something different: one side's client area
stayed black while the other's painted, which happens when the shot lands
early — the wait is 60 seconds rather than 30 because of it. **Re-run before
believing a windowed difference, and look at whether one side is black.**

Windowed still reproduces the screen rectangle byte for byte,
`04000000 1e000000 84020000 fe010000`, so `PositionWindow`'s windowed branch
is confirmed numerically and not by eye. And the Boot Camp figure no longer
has windowed's zero to lean on: 22 scattered pixels on an animating scene is
its own baseline.

**A command-line switch can hide a whole arm of the tree, and `-df` hid one for
the length of this project.** The flag at `0x0047894C` ships as **1** and the
switch CLEARS it, so its name says the opposite of what it does: the packed
`.dat` is the default and `-df` is what selects loose sprite files off disk.
Every configuration in the suite ran with sprites coming out of the archive,
so `SpriteLoadTriple` was a tail call on all of them and its whole loose-file
body — both globs, the failure message, the map-directory prefix — was
unreachable. Two `orig.h` comments had the polarity backwards as well.

`tools/ab.sh df` is that configuration. The install ships exactly one loose
sprite, so it paints the splash from it and reports every other sprite
missing, which is a tighter check than one success: the found arm runs once
and the missing arm twenty times. **Before believing a function is compared,
ask which command line reaches it** — a switch is as capable of gating a
branch as a dialog is.

`tools/ab.sh bootcamp|windowed|intro|audio|df|all` runs the whole thing, and each
configuration now has a pixel budget it must stay inside — 50 for windowed,
which blinks, 500 for the two Boot Camp runs, and none for the intro, which is
two unsynchronised playbacks of a film. Exceeding it fails the run.

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

**AND `make check` IS ALSO A BUILD, which the rule below did not say.** Its
last step links `build/selftest.exe`, which recompiles the shared objects, so
`ab.sh`'s next launch relinks `am2hook.dll` and the two halves of whatever
configuration straddles it run DIFFERENT binaries. Nothing was edited; a
static check was run.

That happened, and the `dll` guard caught it outright: `campaign` came back
`VOID: the two halves ran DIFFERENT am2hook.dll builds`, with the two md5s
printed. The configuration that had already finished stands, exactly as the
note below says.

So the rule is not "do not edit `src/` while a suite runs" -- it is **run
nothing that can rebuild**, which includes `make`, `make check`, and anything
that invokes them. Read a check's recipe before assuming it only reads.

**`tools/ab.sh` REBUILDS, twice per configuration, so do not edit `src/`
while it runs.** Every `play` calls `drive.sh start`, which calls `make -s run`,
which rebuilds and reinstalls `am2hook.dll`. Editing a source file mid-suite is
therefore not "safe as long as I do not build" — it is a build, on the very
next launch.

The hazard is not that the edit goes live. It is that a rebuild landing
*between* a configuration's two halves gives that one configuration two
DIFFERENT DLLs, and the comparison then reports a difference, or hides one,
for a reason that has nothing to do with the reconstruction. Nothing in the
output would say so.

Found by editing three functions during an `ab.sh all` on the belief that only
`make` builds. It happened to land between `intro-recon` finishing and
`audio-orig` starting, so no configuration straddled it and every result stood
— luck, not care. If a source file has to change while a suite is running,
kill the suite and restart it.

**It happened again, and the second time the edit was not intentional at all.**
A staged unit was held back precisely because a suite was running, and then
"dry run" against copies in a scratch directory — except the script's first
line is `os.chdir` to the repo, so `cd`ing elsewhere changed nothing and it
wrote straight into `src/`. The tar that was supposed to make the copies failed
with six `Cannot stat` lines and the script printed `applied` underneath them.
Nine files were modified mid-suite; the run had to be killed with six
configurations to go.

**A script that chdirs is not made safe by running it somewhere else**, and
that is the general form. Anything holding an absolute path — `os.chdir`, a
`$REPO` variable, a hardcoded `/home/...` — ignores the caller's directory
entirely. To rehearse one, point it at a copy by ARGUMENT or edit the path out;
and read what the setup printed before believing the result, because a failed
copy and a successful apply look identical from the last line alone.

What survived is worth stating too: the configurations that had already
finished ran on the old DLL and stand. It is only the ones after the edit that
are void. Say which is which rather than discarding the whole run or, worse,
quoting all of it.

**A vtable slot can point at a stubbed function the harness has claimed, and
`0x0045CAA0` is the example.** One widget class's slot 2 is that address, which
holds a bare `ret` — so it reads exactly like a class whose per-frame update
does nothing. It is `ADDR_LOG`: the retail build stubs the game's own logger to
`ret`, and `src/inject/gamelog.c` patches it to capture output.

What is measured is that vtable `0x0046FD10` slot 2 holds `0x0045CAA0`, that
the address is `ADDR_LOG`, and that patching it silences the game. WHY one
address serves both is inferred rather than established: an empty virtual and a
stubbed varargs logger are both a single `c3`, and identical-COMDAT folding is
what merges such functions.

**THAT IS NOW CHECKED, from a third site.** `0x00460290` -- a qsort over the
def tables -- ends `jmp ADDR_LOG` with an EMPTY argument frame: `retsplit`
says the entry is one function and its caller is one of four consecutive
no-argument calls, so the logger's format pointer would be whatever sits above
the return address. If that address were really the logger, our harness would
capture a garbage line, because `src/inject/gamelog.c` patches it.

It captures nothing -- and the sequence demonstrably runs, because the four
`Object AAI record not found` lines come from `ADDR_DEF_CHECK_LINKS`, the call
immediately after this one. So the logger is capturing at that moment and no
garbage line appears on either side of a `bootcamp` A/B. The address behaves
as an empty function when reached that way, the folding reading is confirmed
behaviourally, and the faithful reconstruction of such a tail call is a plain
`return`.

The measurement cost nothing: it was already in artifacts from a run forty
minutes old. **Before designing an experiment, check whether a run you have
already done answers it** -- the same shortcut that settled `mission`'s stale
frame band from three dated directories on disk.

Reconstructing it as an empty update replaced the logger with a no-op. **The
game then ran perfectly and logged nothing**, which blinds precisely the half of
`tools/ab.sh` that would have reported it — the pixels stayed at their usual 22
and the log went from thirteen game messages to zero on the reconstructed side
only. Five configurations of an `ab.sh all` were spent that way.

`tools/checkpatches.py` catches it in both directions — as a 32nd `ADDR_` alias
and as "0x0045CAA0 patched 2 times: widget.cpp, src/inject/gamelog.c". It was
not run. The three functions were staged deliberately WITHOUT building, to
avoid disturbing a suite that was running; but `ab.sh` rebuilds on every launch,
so the code went live and only the check was deferred. **Deferring the build
does not defer the code — it defers the checking.** If a source edit cannot be
checked now, do not make it now.

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

**A full trace table reads exactly like a missing patch, and it had been full
for some time.** `MAX_TRACED` was 512 with 610 patches installed, so 104
functions — everything patched late in `install()`: palette, sprite, surface,
device, winmain — could not be wrapped, and `counts <name>` answered
"(nothing traced)" for every one of them. Nothing was wrong: `trace_wrap` falls
back to the unwrapped function and the patch goes in either way, and it only
happens under `TRACE=1`. Only the measurement was gone.

Third overflow of that table. It is 2,048 now, with the arena sized from it
rather than separately — the arena holds `ARENA_BYTES/STUB_BYTES` stubs, so
raising one alone just moves which limit bites — and the overflow is COUNTED,
with `counts` appending `[N function(s) NOT WRAPPED: trace table full]`. The
quiet version of this cannot recur. **Before reading a counter as evidence,
make sure the counter exists.**

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

**The MSVC SEH frame on a destructor is NOT reproduced, and that is a
decision.** Several widget destructors open with the VC6 exception prologue —
`push -1; push <handler table>; push fs:[0]; mov fs:[0], esp` — and write an
unwind state index into their frame as they go. None of that is reproduced;
the reconstruction is the destructor's body and nothing else.

What makes it safe is that nothing in this program throws. VC6's `operator new`
answers NULL rather than throwing, and the game tests it — `0x00451251` is
`test eax,eax; je` on the result — so the one plausible source of an exception
is not one here. With no throw, the registered frame is never consulted, the
state index is never read, and the whole prologue is overhead.

State the failure mode rather than only the reasoning. If something DID unwind
through one of these, our frame is not on the `fs:[0]` chain, so the unwinder
would skip it and the base destructor would not run: a leak, not a crash, and
not a wrong answer. That is the cost, and it is accepted knowingly rather than
by not noticing the prologue was there.

**A log message beginning `ERROR:` is not a function naming itself.** The
self-naming sweep matches `Name:` at the start of a message, which works for
`AddMsg:` and `CreateTimer:` and fails for `ERROR:`, `Error:`, `Warning:` and
`List:` — six of twenty candidates. One is worse than useless: `0x004372A0`
prints "ERROR: SetObjScriptState was called with %s", which names a DIFFERENT
function, so a name taken from it lands on the wrong address entirely.

`0x00423200` was listed as "ERROR" and is a DIB loader — it opens a file, reads
a chunk and flips it. The message merely starts "ERROR: %s has listed size of
0". **Read the body before taking the name**, which is the same rule as naming
from a call site, one level further out.

**Say what state the game was in when a global was sampled.** `0x00511E04`
went in as a clock, because `UpdateObjectScript` skips an object while
`obj[0xBC] >= this` and then sets `obj[0xBC] = frame->a + this` — a deadline
against a rising counter. A live probe then read 0x1F4, unchanging, for twelve
seconds of Boot Camp while `ComposeFrame` climbed, and the name was changed to
`ADDR_INPUT_CONTEXT` with "It does not tick" recorded as a fact.

It ticks. The probe was taken with a DIALOG up, and a dialog pauses the game.
Sampled in play with both Boot Camp dialogs cleared it reads 6344, 9427, 12509,
15595 three seconds apart — about 1,027 a second, which is milliseconds. Two
other users agree: `CreateTimer` treats it as `now`, and `ADDR_MOUSE_ACTIVITY`
is stamped from it, which is a timestamp. It is `ADDR_GAME_CLOCK_MS`.

The name was wrong for months on a measurement that was correct and
incomplete. A value sampled only while the game is paused cannot be shown to
tick, and the probe said nothing about which state it was in.

**A mutation that DROPS a term proves nothing when the term is zero.**
`MultiSpritePaint` runs 9,081 times on the multiplayer path, and removing the
vertical bias it applies changed no pixels — which reads as "the bias is zero
on this path" and is a reasonable thing to write down. It was wrong. Adding a
constant five pixels to the drawn position ALSO changed nothing, and so did
returning outright before the blit: the sprite is null on every call and the
function never draws at all.

The two mutations answer different questions. Dropping a term asks "does this
term matter", and a zero term makes the answer no for an uninteresting reason.
Adding a constant asks "does this code run", which is the question you actually
need answered first. **Ask whether the code runs before asking whether the term
matters.**

**THISCALL CLEANS ITS OWN STACK ARGUMENTS, and forgetting that makes a
function unreadable.** `CreateVehicle` opens with `push eax; call
CommMustBroadcast` and then, with no `add esp` anywhere, `pop ebp; pop ebx;
ret` -- which read literally is a function returning into its own argument.
MSVC thiscall is CALLEE-cleanup for whatever is on the stack, so those pushes
are gone by the time the call returns. Four sites in that one function, and
mis-reading one shifts every `esp` displacement after it; it was the single
thing that had deferred the function.

The three shapes to hold together when tracking `esp` through one of these
bodies, all three present in that one function: a thiscall whose argument the
CALLEE pops, a cdecl call whose arguments the compiler cleans LATER and
together with a later call's, and an ARGUMENT SLOT reused as a local once its
argument has been consumed. Any one of them read wrong moves everything below
it, which is why these functions look impossible rather than merely long.

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

**`drive.sh stop` once took the whole login session down with it, and the
mechanism is a shell idiom rather than anything about this game.** The
process-tree walk built its next generation with

```
kids="$kids $(pgrep -P $(echo "$kids" | tr ' ' ',') ...)"
```

and `tr '\n' ' '` leaves a TRAILING SPACE, which becomes a TRAILING COMMA in
the PPID list. `pgrep` reads an empty list element as PPID **0**, so
`pgrep -P "1234,"` answers **1 and 2**. The first pass put init into the list,
the second asked for every child of init, and the `kill -KILL` that followed
reached `user@1000.service` -- `Main process exited, code=killed, status=9/KILL`
in the journal, and every terminal, browser and editor on the desktop with it.

It only fired when a `desktop=amii*` process was still alive at `stop` time,
which is why it was intermittent rather than constant.

Two defences now, because neither is obviously sufficient alone: the walk
carries a `frontier` that is never empty and never has stray whitespace, and
the kill list refuses pid 1 and 2 whatever the walk produced. Anything that
expands a PID list and then signals it wants the second check -- the cost of
being wrong is not a failed test, it is the user's session. A surviving game also
keeps holding `ArmyMenMutex`, which silently makes the next run in that prefix
exit; `tools/drive.sh stop` walks the process tree for this reason.

**`pkill` on a SCRIPT does not kill the game it started, and the leftovers
produce completely convincing false failures.** Three in one session: an
`ab.sh` whose reconstructed side "produced no game log lines"; a black screen
with an empty log while the control socket still answered; and an `mpoptions`
run that sat on the wrong screen because
`control: bind/listen on port 31436 failed (10013)` -- a stale instance still
held the port, so every drive command went nowhere. Each read exactly like a
broken reconstruction and none was.

The mutex is only half of it. The control PORT is the other half, and it fails
differently: with the mutex the new game exits, with the port it runs happily
and ignores everything you tell it. After killing anything, check BOTH before
believing the next run:

```
pgrep -c -f 'ArmyMen2[.]exe'      # want 0
ss -ltn | grep 31436              # want nothing
```

and grep the run's own log for `bind/listen`, which says so outright.

**A SHELL WAITING FOR A COMMAND MATCHES ITSELF, AND THE BRACKET TRICK DOES
NOT SAVE IT.** This file already records `pkill -f 'ArmyMen2.exe'` matching
the killing shell, and the cure -- bracket the pattern. That cure does not
reach the case where the waiting shell's OWN command line CONTAINS the
command it is waiting for:

```
nohup tools/ab.sh bootcamp campaign > out 2>&1 &
while pgrep -f 'tools/ab[.]sh' >/dev/null; do sleep 25; done
```

The brackets stop the pattern matching itself as written, and the shell's
command line still holds the literal `tools/ab.sh` from the `nohup` clause --
so the loop waits for itself and never ends. Three of these were left
spinning across a session, and each one reported its background task as
FAILED long after the A/B it was waiting for had finished CLEAN. The runs
were fine; only the waiters hung.

The reading it produces is the dangerous part and it goes both ways. A
`pgrep -cf 'tools/ab[.]sh'` answered **3** with no suite and no game running,
which is exactly the answer that argues against running `make check` -- and
`make check` is a build, so a false "a suite is running" is as capable of
stopping real work as a false "nothing is running" is of voiding a run.

Wait on the PID (`wait $!`, or `kill -0`), never on a pattern that the
waiter's own arguments contain. And when the question is really "is it safe
to build", ask the two things that cannot match a shell: `pgrep -c -f
'ArmyMen2[.]exe'` and whether the control port is held.

## Open items

- **The Lock/Unlock bracket batch is a different goal from the boundary, and
  its numbers were wrong.** It said "5 of 22 done" and named `DrawText` and
  `DrawSprite` among them; neither calls `LockSurface` or `UnlockSurface` at
  all. Measured: **29 functions** call the bracket and **29** are reconstructed
  — the bracket is COMPLETE, closed by HudSquadDetail, and the rasteriser
  goal this item tracks is finished
  — `RenderGlyph`, `RedrawMapRegion`, `CalibratePalette` and `DrawMenuCursor`,
  the last of which the old list predates, and the menu-widget painters that
  have landed since.

  Do not hand-edit that pair. `tools/checkclaims.py` recomputes it, and it is
  the reason this sentence is right: the count moved from 10 to 11 the moment
  `TyperPaint` was written, and the check failed the build rather than letting
  the prose go quietly stale — which is the whole argument for the tool.

  `0x00454F00` came off the shortlist as `LabelDraw`, and it opened a subsystem
  rather than closing a rasteriser: it is vtable slot 1 of a menu widget class,
  one of **thirty-three five-slot vtables** laid out consecutively from
  `0x0046FAB8` to `0x0046FD38`, each referenced by exactly one constructor and
  one destructor. So the menus are a class tree with five virtuals apiece, the
  edit box (`0x00454C10`, whose focus method installs `g_charHandler`) is the
  class one entry earlier, and `src/game/win32/widget.cpp` is where the rest of
  it goes. A vtable array is worth walking the moment one of its slots is
  reconstructed — it says how big the subsystem is before any of it is read.

  **The five slots are 0 destructor, 1 paint, 2 update, 3 focus, 4 repaint**,
  and naming them needed the whole array rather than any one vtable: slot 3 is
  the same function in 30 of the 33 and slot 4 in 29, so those are the base's
  and everything else is an override.

  **Slot 2 went in as "click" and that was wrong**, from a glance at
  `0x00454BD0` that saw a function pointer being called and stopped there. Its
  callees settle it: the three queries around the call are `IsKeyDown`,
  `KeyChanged` and a consume, the scancode is 1 — ESCAPE — and
  `!down && changed` is the key being RELEASED, the same idiom as the
  in-mission ESCAPE handler. It is the per-frame update, `0x00454BD0` is the
  override that gives a dialog its cancel key first, and the base at
  `0x00453E80` places the widget and recurses into its children — which is why
  `WidgetScreenRect` runs a million and a half times. **Name a virtual from
  its callees, not from the shape of its body.**

  **Drive the input and see where the game ends up.** The menu layer turned
  out to be checkable far more sharply than by comparing frames.
  `WidgetUpdate` is a dialog's whole keyboard interface — UP and DOWN and TAB
  move focus, SPACE and RETURN repaint the focused child and then fire its
  handler on RELEASE — and four of its five branches were confirmed in one run
  by pressing the keys: DOWN walks the OPTIONS highlight AUDIO → CONTROLS →
  DIFFICULTY, TAB does the same to the pixel, SPACE opens CONTROLS, RETURN
  opens SELECT DIFFICULTY. ESCAPE closes a dialog through `WidgetUpdateCancel`
  the same way.

  This beats the A/B on its own ground for anything that causes a state
  transition. It needs no second run and no budget, and it discriminates a
  wrong scancode constant — which an A/B never can, because both sides are
  driven with the same key and would agree about ignoring it.

**`drive.sh ctl widgets` dumps the widget tree, and it is an EXACT oracle
where the pixels are a blunt one.** The menu layer's defects are too small for
a whole-frame comparison — measured, not guessed: a wrong toggle sprite is 212
pixels, a WM_CHAR handler that is never installed is 72, an unrepainted list
row is 0, and two flags the base constructor writes are 0. Every budget that
survives a blinking caret is above all of those.

The state those defects live in is in the tree. `widgets` walks it from
`0x0065A058` — where the dialog opener at `0x00451210` stores whatever dialog
is up — and prints each node's rectangle, vtable, sprite, focused child and
flags, with pointers renumbered in first-seen order the way `tools/actdiff.py`
renumbers them. The CONTROLS dialog is 25 nodes and they come back **byte for
byte identical** from the original and from the reconstruction, so `ab.sh`
compares them with `diff` and no budget at all. Setting the base constructor's
`0x0050` to 0 — invisible to all three pixel frames — changes all 25 lines.

**A first-seen index cannot see a SUBSTITUTION.** Pointers are renumbered so
the dump survives the heap moving, which is the same trick `tools/actdiff.py`
uses — and it made the oracle blind in exactly the way it was built to fix.
Forcing `TogglePaint` to the wrong sprite left the tree identical, because the
substituted sprite is first-seen at the same position and takes the same index:
`spr=10` on both sides, 212 pixels apart on screen. The sprite's own `id` is
printed beside the index now, and reads 1576448 against 1576449. **Renumbering
buys reproducibility and blindness in the same stroke — carry one real datum
beside every renumbered pointer.**

**Two ways a debug dump can take the game down, both hit here.** Reading the
sprite id without a range check faulted; and the pointer was read in the
declaration's initialiser, which runs BEFORE the `if (!w)` guard below it, so
every null child faulted. Both closed the control socket mid-reply, which fails
the run the dump was meant to explain. A diagnostic that can crash is worse
than no diagnostic.

**Field `0x0040` is deliberately NOT in the dump.** It is the one the
constructor never writes, because `ButtonUpdate` computes it before anything
reads it, so for every widget whose update has not run it holds whatever the
allocator left: it came back as 25, 1 and 27,346,604 on runs that were
otherwise identical. Two runs compared by hand happened to agree, which is how
it got into the first version. **An uninitialised field cannot be part of an
exact oracle**, however meaningful it is when it is set.

    **`tools/ab.sh controls` is the menu A/B.** That dialog is
  78,174 `LabelDraw` calls — every caption from "SARGE CONTROLS" to "EXIT
  VEHICLE" — and the dialog itself comes out **0 of 786,432**. Its budget is
  200, for the cursor and nothing else; see below.
  Two clicks from the title screen, no typing and no mission:
  the cheapest gameplay-free configuration in the suite, and the only one that
  compares the menu widget layer at all, since `bootcamp` and `campaign` merely
  pass through the menus and the game composes no frames while a dialog is up.

  **Three runs agreeing is not determinism, and this budget was 0 for exactly
  as long as it took a fourth run to disagree.** Driving it by hand first gave
  54 pixels in a 10x13 box at the cursor. That looked like an artefact of two
  clicks landing at different moments, and three `ab.sh` runs at 0 seemed to
  confirm it — so the budget was tightened to 0 with a comment saying it had
  been measured rather than reasoned. It had been measured; three times is
  simply not enough for something that happens about one run in five. The
  fifth run came out at 45, in the same 10x13 box.

  So the pointer is not reproducible frame for frame even when both sides are
  driven identically, and 200 covers the box. **When a figure is going to
  become a budget, ask how rare a disagreement would have to be to hide from
  the sample you took** — three clean runs cannot distinguish "never" from
  "one in five" with any confidence at all.

  What survives is the useful half: the DIALOG is exact, and the difference
  when there is one is entirely the cursor. That is worth knowing, because it
  means a handful of pixels here is never a caption.

  It fails when it should: clearing the label background with the ink colour
  rather than the paper colour puts it 17,110 pixels over.

  **It takes TWO shots, and any configuration may.** `ab.sh` compares every
  frame a run leaves behind, not only the last one — `controls` grabs the
  OPTIONS menu between its two clicks, and the comparer checks both against
  the same budget. That was added because a menu is mostly transients and a
  settled final frame cannot show one.

  **It did not do what it was added for, and that is worth knowing.** Three
  mutations that are genuinely wrong code passed the single-frame version —
  `WidgetTakeFocus` focusing the obvious widget instead of the parent's first
  child, `WidgetRepaint` never deferring to an ancestor, and both flags
  `WidgetConstruct` writes as 1 — and all three still pass with the second
  frame in. So the sample was not too late; that state simply does not reach
  the screen on either of these two. The second frame is still worth having,
  because it is discriminating on its own (93,347 pixels for a
  `WidgetScreenRect` error, independently of the final frame's 305,939), and
  it covers a screen nothing else did. **Mutation-check an extension of a test
  before crediting it with anything** — extending a test is not the same as
  extending its reach.

  The sizes quoted for the next candidates were off as well (`0x00413610` is
  128 B, not 256; `0x00433350` is `0x00433360` at 288 B), which is what
  `tools/merges.py` was written to fix.

  Worth being clear about what this item is: the bracket finds the game's
  software RASTERISERS, which are a rewrite goal of their own. It is not the
  Win32/DirectX boundary and finishing it is not required for that boundary to
  be complete — every lock in the image already goes through our `LockSurface`.
  The line trio is done -- `DrawVLine` (`0x0041CBA0`), `DrawHLine`
  (`0x0041CC40`) and the `DrawRect` (`0x0041CDC0`) that calls both, all in
  `win32/mapdraw.cpp`. Worth knowing before taking the next one: both line
  drawers Lock and never Unlock, so the pairing is the caller's, and several of
  the 29 will be half-brackets like that. `DrawViewRect` (`0x00413610`) is the
  matching half for all three -- it Locks once, draws the whole outline, and
  Unlocks once -- so **the pairing is per FEATURE, not per function**, and a
  count of "functions calling the bracket" will keep finding halves. Worth knowing too that none of the
  three executes on any drive this project has -- being a rasteriser does not
  make a function reachable.
  **That shortlist listed functions that were already done**, which is what a
  hand-kept queue beside a ratchet always comes to: it named `0x0041CC40`,
  which is `DrawHLine` and had been reconstructed for some time, and
  `0x0041C7F0`, which is `DrawBlip3` and went in the same day this sentence
  was corrected. A count that only goes up cannot tell you a candidate has
  been taken; only re-reading the list against the patch list can.

  **And it did it again one entry later**, listing `0x0041C8A0`, `0x0041CA50`
  and `0x004149B0` as outstanding when all three -- `DrawBlipPulse`,
  `DrawBlipSquare` and `RadarBlipColour` -- had landed with the radar. The
  radar's five primitives are now all ours. So is `DrawSelection`
  (`0x00462120`, 688 B), the leader's caret and the selected units' health
  bars, which is the first of the batch that ordinary play actually reaches.

  Rather than keep writing the queue down, generate it -- the same argument
  that put the count in `tools/checkclaims.py`.

  **THE QUEUE IS EMPTY AND THE HAND-KEPT VERSION WAS WRONG IN BOTH
  DIRECTIONS.** It said "**four** functions and none of them small" and then
  listed TWO, `0x00462600` and `0x00416340` -- a count and a table that had
  stopped agreeing with each other, in a paragraph whose whole argument is
  that queues should be generated rather than written down. Both are
  reconstructed now, checkclaims reads (29, 29) for the bracket, and
  tools/remaining.py reads 0 game functions.

  So this entry is closed, and the way it failed is the argument for the
  advice above it: the list drifted from its own count before it drifted from
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
- **A PROBE THAT REDEFINES THE FUNCTION'S NAME ALSO REDEFINES ITS PATCH.** The
trick that works for a blind counter -- `#define Fn ((FnType)(uintptr_t)ADDR_FN)`
so the caller goes through the detour -- rewrites the `patch_replace(ADDR_FN,
(const void *)Fn, ...)` line too, so the address is patched to jump to itself.
The counter then reads whatever the corrupted stub leaves in the slot: 752
million, then two billion ten seconds later, which is not a call count of
anything. Reverted, and the reading stands on the disassembly instead.

The version that worked on `ApplyObjFrame` put the macro in the CALLER'S file,
where there is no `patch_replace` to catch. **Put the probe macro where the
call is, never where the install is** -- and treat an implausible counter as a
broken probe before treating it as a result.

**AN ARGUMENT ORDER IN `orig.h` IS A GUESS UNTIL SOMETHING READS THE
PROLOGUE, and one of them propagated into a reconstruction and stayed wrong.**
`ADDR_ENTER_VEHICLE` carried `/* void(vehicle, unit) */`; the unit is first.
The reconstruction was written from that comment and then written CONSISTENTLY
with it, so the seat check ran on the unit and the boarding uid went into the
vehicle -- every field access on the other object, with nothing inside the
function looking wrong and nothing for the compiler to say.

Two instructions settle it and neither is subtle: `mov edi, [esp+0xC]` before
the second push is the SECOND argument and carries VEHICLE_OFF_SEATS,
VEHICLE_OFF_PTR_LIST and the army the broadcast is gated on; `mov esi,
[esp+0xC]` one push later is the FIRST and carries OBJ_OFF_RIDING,
OBJ_OFF_SARGE and the DestroyByType at the end.

It was found by reading a CALLER for something else entirely, and confirmed by
a second caller that reads OBJ_OFF_SOLDIER_KIND off the argument it passes
first. **When a prototype in `orig.h` names two arguments of similar type, it
is a guess unless its comment says which instruction fixed it** -- and a
reconstruction that inherits the guess cannot detect it from the inside.
`checkoffsetuse` cannot see it either: the offsets are all still there, just
on the wrong pointer. The sibling `BoardVehicle` was checked the same way and
is correct, as are `SendVehicleEnter` and `SendVehicleExit`.

**Name a function from its body, not from one call site.** Two instances now,
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

  **AND SWEEP BOTH ENDS, because a sweep from one misses what the other has.**
  The six names below came from the RECEIVER's switch and are complete for
  what `WndProc` handles -- which is exactly why `0x0468` was not among them.
  `CommSystemMessage` posts it after a player joins, it is the only site in
  the image that does, and nothing handles it at all: `DefWindowProc` eats it.
  A message with a sender and no receiver is invisible to a sweep of either
  end alone.

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

  `g_charHandler` is NOT the same kind of thing. It is a slot, not a function:
  the menu's text fields write their own consumer into `0x005125B8` and
  `WndProc` just calls whatever is there. Porting "it" meant porting the
  text-field system, and this entry said so as a reason to leave it alone.

  **The text-field system is ours now**, so the argument expired rather than
  being overturned: the edit box, its focus method and `EditCharHandler`
  (`0x0044D520`) are all reconstructed, and `EditTakeFocus` installs the
  handler by NAME. The slot still holds whatever the field put there — what
  changed is who wrote it. `0x00417790` and `0x00418480` are the two
  still-original fields that are not the menu's.
- **`-w` is windowed mode**, global `0x00507344`, and it gates far more than it
  looks: the window border and repositioning, the palettized primary in
  `InitDirectDraw`, and `CalibratePalette`. Anything that reads 0 under the
  default fullscreen run may simply be behind it. The other switches are in
  `orig.h`; three are developer names, and `-rob` is the flag that was already
  known as `ADDR_DEBUG_ITEMLIST`.
- Windowed mode runs and is worth using as a second configuration — the window
  is created, sized and positioned correctly (client area 640x480 at (4,30))
  and `CalibratePalette` fires. **It no longer stays black**: this entry used
  to say Wine hands back no lockable primary and the client area never draws,
  and that is not what happens here now — the area paints, mostly white, with
  a blinking 10×10 element in it. White rather than the title art is still not
  right, so fullscreen remains the configuration to verify against; what
  changed is that "it is black" can no longer be quoted as the reason a
  windowed comparison is trivially exact.
- Both DirectDraw `Restore` paths are untested. `LockSurface`'s is a real defect
  in the original — it publishes an uninitialised descriptor after a successful
  Restore without re-locking. Kept as-is deliberately; see `src/game/win32/surface.cpp`.
- **NO CONFIGURATION IN THE SUITE REACHES COMBAT, and that is one measurement
  rather than a dozen separate mysteries.** On a Boot Camp mission driven past
  both dialogs, with `ObjIsItem` climbing past 84,000 and the frame ticking,
  `ShotStrike`, `ApplyShotDamage`, `DamageObject`, `CreateMissile`,
  `FireWeaponAtPoint` and `FireWeaponAtObject` ALL read 0 -- and none of those
  counters is blind. Sarge stands there; nothing fires and nothing is hit.
  Pressing SPACE, both CTRLs, ALT and RETURN changes none of them.

  So every function whose only route is "something took damage" is cold for
  ONE reason, and the entries below that used to explain themselves
  individually -- `RemoveFromItemList`, `FreeItem`, `ShooterReact`,
  `ApplyObjFrame`'s DamageItem route -- are the same fact said four times.
  Getting any of them exercised needs a drive that makes a unit shoot, which
  this project does not have and which would be worth more than several more
  reconstructions: it would unlock the whole combat-consequence layer at once.

- **`RemoveFromItemList` is unexercised for a now-known reason.** Its gated
  caller is `FreeItem` (`0x004285F0`), which dispatches on the item kind and is
  the only route that unlinks. Neither runs on a campaign drive: 325 items are
  added during load and none is destroyed in the ~25 s observed, because
  nothing in that window shoots anything. Reaching either needs a mission
  driven long enough for something to die, which is a drive this project does
  not yet have -- not a missing code path.
- `CheckSaveTag` executes; it is reached by the save-file header read at
  `0x00425950` on any campaign start with a save present. The entry below
  predates that and is left for the others.
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

- **"UNEXERCISED" IS NOT "UNVERIFIED", and 12 of the 32 below are
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

  The other twenty are verified by READING, which is the standing worth
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
  `ab.sh` produces. `RefreshScreen` has 7 callers and "whatever forces an
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
  `ADDR_MENU_MODE` reads **33** throughout Boot Camp play, which is
  why the ESCAPE arm — number 34 — never runs. And `ADDR_STATE_WANTED` really
  does sit at -1 while nothing is pending, as `orig.h` claims.

  **`StopNamedSound` is still unexecuted, and the reason is now the LEVEL
  RECORD rather than a mystery.** Its only call site is `0x00424DC3`, guarded
  by the name buffer at `0x00511D58` being non-empty -- and that buffer is
  filled by `SelectLevel` (`0x0043ED50`) from the chosen level's record, field
  +0x288. Boot Camp's record leaves it empty, so nothing is ever named to be
  stopped. A level that names one would reach the call. That buffer stays
  all-zero for an entire Boot Camp mission — polled repeatedly — so nothing is ever named to be stopped.
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
- **The statement layer is complete**: `ReadScript` and all five handlers are
  reconstructed. It dispatches on exactly six ids, the same set
  `ScriptIsStatementStart` answers yes for -- `preloadsprite` (25), `pad` (26),
  `if` (44), `variable` (133), and `object` (139) and `objclass` (140) sharing
  `GenerateObjScriptFromTokens`, which is a real source name recovered from
  that function's own error string.

  **NOTHING BELOW A STATEMENT IS STILL ORIGINAL, and this paragraph said
  otherwise for a long time.** It listed the event parser (`0x0043FF90`), the
  event-list parser (`0x00440600`), the testvar value parser (`0x00443010`),
  the 8,608-byte action parser (`0x00440D70`) and ScriptRunLine
  (`0x00444C40`) as reached by address. All five are reconstructed --
  checked against the patch list, not assumed from the total -- and the
  parenthetical "now reconstructed" against just one of them is the tell: a
  list corrected in place, one entry at a time, until only the sentence
  around it was wrong.

  **The handler A/B has better evidence than a log match.** `ReadScript` prints
  four totals that count exactly what the handlers produce -- Boot Camp
  `lines: 101  tokens: 372  names: 43  compounds: 16` and campaign
  `lines: 1225  tokens: 2895  names: 316  compounds: 91`. `names` counts what
  `variable`, `pad` and `object` declared; `compounds` counts the `if`
  statements that parsed. Four independent numbers agreeing on both sides is
  worth more than "the log is identical".
- **Object types 2, 3 and 8 are identified now, and the answer had been in the
  tree for some time.** Type 2 is a TROOPER -- `FreeItem`'s arm for it logs
  `"DestroyTrooper %x"`, so the program names it. Type 4 is a WEAPON on the
  same evidence. Type 3 is a VEHICLE by two independent routes: `FreeItem`'s
  arm is `DestroyVehicle`, and the type-3 destroy handler clears a footprint
  out of `ADDR_VEHICLE_MASK` indexed by a kind. Type 8 is a ROACH, from the
  matching clearer that indexes `ADDR_ROACH_MASK` with no kind index at all.
  The table is in `orig.h` above the type predicates.

  Worth noting HOW it stayed open: nothing was missing. `DestroyTrooper` had
  been named from its own log string, and `FreeItem`'s switch had been
  reconstructed with all its arms, and this line went on saying unidentified
  because nobody put the switch beside the question. Before recording something
  as unknown, grep the tree for what already answers it.

- **Type 5 is a MISSILE, and the evidence is the loader.** `LoadType5`
  (`0x0043B870`) calls `ObjInitCommon` with 5 and then puts
  `ADDR_MISSILE_ANIMS` -- missile.ani -- into the row it builds, which is
  exactly how type 8 was settled as a roach and type 3 as a vehicle. Its whole
  record is 0xB8 bytes against a roach's 0x560, and its box is six units
  square, both of which fit.

  What made it findable was the same thing that made the roach findable: the
  per-type SAVEGAME LOADER is where a type's constants all appear at once --
  the box, the row spec, the anim table and the def record. Reach for
  `LoadTypeN` when a type is unidentified, before anything else.

  **Type 7 is the last one unread.** Type 6 is an EXPLOSION -- `StepType6` and
  the `BLAST_OFF_*` fields settled it, and `orig.h` records that its offsets
  are overloaded by type, which is what cost a defect the day it was found.
  Type 5 is a MISSILE, from `LoadType5` putting `missile.ani` into the row it
  builds. So the shared `FreeItem` arm holds types 1, 5, 6 and 7 and says
  nothing about any of them -- demonstrably, since three of the four are now
  identified from elsewhere and none of it came from that arm.
- **`object.aai`'s `link 33-1..4` complaint is explained, and it is data
  rather than a defect.** The message is "Object AAI record not found for link
  %02d-%-3d", emitted by `0x00435FD0` -- a post-parse validator that qsorts the
  link table (comparator `0x00435EB0`, stride `0x14`) and then checks every
  parent key against the AAI records. So the numbers are a parent TYPE and four
  link numbers, and the file declares links from a parent it never defines. It
  fires identically under `AM2_NOPATCH=1`, which is what settles that it is the
  original's behaviour: `tools/ab.sh campaign` compares the log and passes with
  all four lines on both sides.
