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

**`make check` runs everything that does not need the game.** **67** analysis
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

**AND IT KEPT A SECOND LIST OF ITS OWN, which drifted from `make check`'s
within one commit.** `ORACLES` in `checkclaims.py` names the tools whose
subjects count toward the verified split, and `make check`'s recipe names the
tools it runs. Adding `pathplancheck.py` to the recipe alone left the split
reporting 28 when the tool it had just gained made it 29 -- so a check that
exists to stop numbers going stale had a hand-kept list going stale inside it.

The two are not the same set and cannot simply be merged: `scriptcheck` is in
one and not the other. What is checked instead is the direction that bites --
a tool declaring `CHECKS` and missing from `ORACLES` is an ORPHAN and fails
the run, because a declaration nothing reads is a check nobody is counting.
Tested by removing the entry again, which reports it by name.

**AND IT NOW READS `STATUS.md` TOO, which had drifted on every number in
it.** That file summarises where the work is, and nothing was checking it:
1,641 patches against 1,643, 41 analysis tools against 57, and a verification
split of 12 of 32 against 31. Its own header says it "can be stale between
updates" -- which is the warning this file already says is not a defence
against the thing it warns about.

Two of its three numbers are checked, and the third is deliberately left. The
patch count belongs to `checkpatches.py`, which exposes no function to ask,
so checking it here would mean a second copy of that scan -- the drift this
is meant to stop, one level down. Say which numbers a check covers and why
the rest are absent.

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

**`tools/checkcallers.py` ASKS THE OTHER HALF OF THE BLIND-COUNTER
QUESTION: not "can this counter move" but "can this function be reached at
all".** A reconstruction whose callers in the image are ALL reconstructed is
reachable only from our own source, so if nothing in `src/game` names it, the
detour is installed and nothing jumps to it. That is `Step3Input` exactly --
1,424 bytes of a driven vehicle's whole input handling, installed, counted as
done by every tool, and called by nobody, because our `StepType3` had lost the
arm that reaches it. No vehicle in the game could be steered and every check
was green; its counter read 0, which is what a healthy function reached from
our own code looks like.

It found three more on its first run and they are in `KNOWN`, a baseline that
may only go down. The useful tell is that `MsgSlotB0` and `MsgSlotA2` pass
while `MsgSlotB1` and `MsgSlotB2` fail: same caller, same family, so it is a
missing dispatch arm rather than a dead function.

**`tools/espmap.py --site=0xADDR` answers what ONE instruction reads**, which
its per-slot listing cannot: that truncates to four addresses, which is fine
for a survey and useless when you are reading a call site's arguments. It
prints the frame slot, which argument it is, and how many bytes of outstanding
push shift the raw displacement, so a `[esp+0x24]` that is really ARG2 says
so. **Run it on every argument-slot read in a body that pushes.** Two defects
in one afternoon were exactly that shift, and each had a correct sibling four
lines away: `Type2PlayerInput` read the OBJECT's +0xC0 where the original
reads the WEAPON's, so shift-clicking to fire dereferenced NULL and killed the
process; `FireWeapon`'s lobbed arm passed the clamped RANGE where the original
passes the launch HEIGHT, so a grenade aimed further flew shorter.

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

**THE HYBRID RUNS THE ORIGINAL OVER OUR PLATFORM, and it is the check the
native build cannot be.** `make hybrid` is a PE loader of our own: the
retail image mapped at `0x00400000`, its import table bound by module and
name from `src/platform`'s export table, a TEB per thread for the SEH chain
at `fs:[0]` -- the only TEB field the image touches, 340 sites -- and the
MSVC CRT's entry point called. Every call the original makes out of itself
lands in the same platform layer the reconstruction runs over, so a defect
in that layer that the reconstruction happens to agree with shows up here.
Its first run found two: STATUS.md has them.

**A VTABLE'S SLOT ORDER IS NEVER PRIVATE.** `ddraw.h` said it was, because
the reconstruction reaches DirectX through name macros, and reordered two
slots for a tidier macro. The original indexes by number, and the very first
DirectDraw call after `QueryInterface` landed on the wrong method with the
wrong arity. `tools/checkvtables.py` compares every platform vtable against
the SDK's declaration slot by slot, and is in `make check`. The rule
generalises: any layout the ORIGINAL sees -- a vtable, a struct the image
carries, an import ordinal -- is the SDK's, and "nothing indexes it" is a
claim about today's callers, not about the layout.

**GDI TEXT IS REPLAYED FROM WINE, NOT RASTERISED.** The game builds its
three fonts from `TextOutA`'s pixels, so text is exactly as good as the
rasteriser, and stb_truetype disagreed with Wine's FreeType on about 1,400
pixels of one dialog. `tests/glyphs-reference.txt` is those fonts read out
of the ORIGINAL under Wine by `tools/glyphdump.py` (672 glyphs, the number
`font.h` already gives for a session), `src/platform/glyphs.inc` is
generated from it, and `gdi32.cpp` paints the recorded bitmaps for those
faces. To re-record: drive the injected build to the title
(fonts 1 and 2) and into a mission (font 0), and run the tool with `--port`
at each. The `.inc` is in the drift check, the reference is not derived
from anything in this tree, and a face the game never asks for still goes
through stb_truetype.

**A DIFFERENCE INSIDE THE POINTER'S BOX IS HISTORY BEFORE IT IS RENDERING.**
The hybrid's HQ-dialog frame matched Wine's everywhere but the pointer,
and two hours went on palettes, remap tables, stretch rounding and
animation phase before the cursor was simply moved away on both sides:
the residue it left was the difference, and both sides had it. The pointer
saves and restores only its arrow; its overlays are software sprites nothing
restores, so that spot holds whatever was drawn there last, which two
unsynchronised drives never agree on. Ask what the pointer LEAVES before
asking what it draws, and compare on a screen that repaints the spot.
The stretch rounding was a real finding on the way -- the platform now steps
wined3d's truncated 16.16 increment -- but it was not the cause.

**`-isystem` HEADERS ARE INVISIBLE TO `-MMD`, AND A HEADER CHANGE THEN
REBUILDS ONE SIDE OF AN INTERFACE.** Reordering the platform's `IDirectDraw`
vtable rebuilt `ddraw.o` and left every reconstruction object that called
through it stale, because their dependency files did not list a header
included via `-isystem`. The native binary crashed on its first DirectDraw
call for two sessions and nothing in the build said why; the bisection
"fixed" it by reverting the header, which merely matched the stale
objects again. `DEPFLAGS` is `-MD` now. When a header change breaks a
binary that includes it, clean-rebuild before reading a single line of
code.

**LOCKSTEP IS THE PLATFORM'S TO PROVIDE, and it made "frame-by-frame" a
real comparison.** Every source of nondeterminism between two runs was in
`src/platform`: the clocks, the multimedia timer thread, the audio thread,
input timing. `AM2_LOCKSTEP=1` replaces all of them with a pump count,
`AM2_REPLAY` scripts input by pump number, `AM2_FRAMELOG` hashes every
presented frame, and `tools/lockstep.sh` runs two binaries headless and
names the first frame that differs. The hybrid against the native build
diverged on frame 80 of Boot Camp by 26 pixels and on nothing else through
a walk, with identical object tables at both ends. A hash log is exact
where the pixel budgets were blunt; reach for it before any screenshot.

**THE SIDE-BY-SIDE IS THE LOCKSTEP PLAYED LIVE, AND ITS FIRST HOUR FOUND
WHAT `lockstep.sh` HAD CALLED CLEAN.** `tools/sidebyside.py` runs the
hybrid and the port under `AM2_STEP`: each game blocks on a socket before
every pump, reports the frames it presented and the host input it took,
and the coordinator hands the leader's input to the follower, steps it
through the same pump and compares. On a difference BOTH games stop where
they are, alive, so `tools/objdump.py --port`, `peek`, `poke` and `snap`
answer at that frame; `--video` gives the leader a real window to play in,
`--break N` and the prompt's `s`/`c N` stop at a pump, and
`--tolerate-swaps` forgives the known terrain class -- horizontal pairs
with their two colours exchanged -- which grows with the scroll and so
cannot be a pixel budget.

`lockstep.sh` diffs the FIRST differing frame and STATUS.md said every
frame after it "differs by the same 26 pixels". That was inferred from the
object tables agreeing, and it was wrong: with the 26 tolerated the next
frame past them was the port showing the LIVE MISSION where the original
showed Boot Camp's full-screen instruction sign, and the port kept
composing frames through fifty pumps the original spent paused. The object
tables agreed because nothing moves in a sign. Stepping both games pump by
pump with the timer table, the clock and the sub-state read out over the
sockets took it to one line in one function, below.

**TWENTY MINUTES OF HAND PLAY FOUND TWO DEFECTS NO REPLAY HAD REACHED, and
both were argument slots.** The first shot anyone fired in the port
detonated on Sarge's own position: `DefWeaponLine` read the twelve numbers
of a weapon line in order, and the original stores the FOURTH at rec[5]
and the FIFTH at rec[4] -- two parse blocks whose `lea` sits on the other
side of a push from their neighbours -- so every weapon's range was its
following field and the rifle's was 0. Then a held click aimed two facing
units off: the held arm of `Type2PlayerInput` takes the distance to the
raw point and the BEARING to the overlay-adjusted one, +0x10 and +0x0C,
and both had been written as the raw point. Neither is visible to any
replay in the tree, because no replay fires or holds a click over the map;
both were in the first minute of someone playing.

The way each was found is the method, and it is cheap once the tool
exists. Record the session (`input.txt` is a replay file). Reproduce the
trap headless. Step both games pump by pump over the sockets, dumping the
one record that differs, until the FIRST pump on which they diverge --
Sarge's fire point, then his facing. Grep the image for every store to
that offset (`disasm` over `.text`, a `mov` whose destination carries the
displacement) and match the sites to the reconstruction. When the
reconstruction's writer looks right, log its INPUTS on both sides:
`AM2_TRACE_ANGLE` installs a logging copy of `AngleBetween` over the
original in the hybrid and the same log in the port, and one line each --
`to=(1834,1057)` against `to=(1834,1060)` -- named the slot. Then
`tools/espmap.py FUNC --site=ADDR` on every point the function passes,
which is the check that should have been run when it was written.

**THE THIRD WAS A FLOAT PARSED AS AN INTEGER, and the parse blocks were
identical except for the call target -- the DefWeaponLine lesson again.**
`DefObjLine` read thirteen numbers; the original's ninth call is
`DefParseFloat`. That field is the object's depth SLOPE, the one
`DepthCompare` projects with, so the rifle-range sign's -0.35 became 0
and the hut's 2.06 became 2, and the port drew a sign post over Sarge's
rifle. The object tables cannot show it -- the slope lives in the def
record -- and no replay walks past the sign. What settled it was
`AM2_TRACE_DEPTH`: the reconstruction's comparator installed over the
original in the hybrid, logging both records' keys, beside the same log
in the port; the first differing line read `s-0.35` against `s0`. When a
parser's blocks all look alike, list the CALL TARGETS before believing
the repetition: three defects in these two parsers were exactly that.

**THE FOURTH WAS ARGUMENT IDENTITY, four instructions from the call.**
`TrooperPickupItem`'s swap arm pushes ESI -- the picked-up ITEM -- at
0x0044865F, then stores three fields through EDI and EBX, then calls
DestroyByType. Read with the nearest register, it destroyed the HELD
weapon; the original marks that one replaced for the item sweep and takes
the new one off the map. Both games log `FreeItem` for the old rifle, by
different routes, so the log agreed and the picked-up weapon stayed on the
map, drawn over Sarge as he carried it. The push is the argument, wherever
the call is.

Four defects in an hour of play, and every one of them is a class this
file already names: a slot on the other side of a push, two locals four
bytes apart, identical blocks whose only difference is the call target, a
push separated from its call. None was an offset, so `checkoffsetuse`
passed all four; none is reached by a scripted drive, so every A/B passed
all four. What they share is that each was READ as a pattern -- twelve
blocks alike, two pointers alike -- where resolving the operand
mechanically would have answered in seconds. The rules were written; they
were not run. `tools/espmap.py --site` on every slot, the call target of
every block, and the pushes of every call are the mechanical form, and
now every recorded session is a regression that runs in a minute.

**A LIVE DIVERGENCE THE RECORDING CANNOT REPRODUCE IS THE TOOL'S, and the
tell was a round number.** After a trap the live run diverged by half the
frame while the headless replay of its own recording ran clean, and so
did the replay paused at that pump for forty seconds with the sockets
busy. The recording held exactly 64 host lines at the pump after the
trap: the platform kept host-event notes in 64 fixed slots, a held window
queues hundreds of motions, and the 65th onward reached the leader and
never the follower. The buffer grows now. When the replay disagrees with
the run it was recorded from, suspect what carries the inputs before
either game.

**"ONLY THE EFFECT IS REPRODUCED, NOT THE UNROLLING" WAS THE 26 PIXELS.**
`BlitOverlay`'s comment said its 656 bytes align the destination to a
dword and transform four pixels at a time with a lead-in per
misalignment, and that a per-byte loop is indistinguishable in result. It
is not: the lead-in for misalignment 1 with exactly two pixels left
(0x0041C57C) maps both bytes and assembles the word as `(lut[first] << 8)
+ lut[second]`, so the store puts the second pixel first. Every one of
the 26 pixels that separated the port from the original from frame 80 of
every comparison was a two-pixel shadow run at odd x whose two pixels
differed. Found by asking which blit last wrote ONE of them
(`AM2_TRACE_PIX=x,y` in blit_core) and reading that blitter's every arm.
An unrolled loop is the original's behaviour, quirks included; "the
effect" of a loop is only known once every lead-in and tail has been
read, and the claim that they all agree is a claim about each of them.

**THE COMPARISON IS EXACT NOW, and stays exact.** Every replay under
`tests/replays` runs identical, hybrid against port, and the rule from
here is the one Boran set: nothing forgiven, the first differing frame
is the issue, a new divergence found while fixing one is fixed first, and
the open list is the top of STATUS.md. The tolerance switches on
`tools/sidebyside.py` remain only for REACHING a later pump past a known
difference while it is being fixed; they are never a verdict.

**AND A RECORDING IS THE FIRST THING A LIVE TOOL NEEDS.** The very first
live trap -- Sarge diving prone in the original and standing in the port
-- was lost, because nothing had written the inputs down. Everything
found since came from `input.txt`.

**AND THE ORIGINAL'S OWN EVENT TRACE IS ONE `poke` AWAY IN THE HYBRID.**
Every log line in the event layer is behind the comm object's
`COMM_OFF_VERBOSE`, so writing 1 there through both sockets at a break
before the trigger gave `EventTriggerDelayed: type 0, num: 100005` from
the original beside `num: 0` from the port -- the whole diagnosis in two
lines, where the timer table had been identical on both sides. Prefer the
program's own trace to reasoning about it, and remember the flag is a
field on a heap object, not a global.

**INDEXING A TABLE WITH THE CALL THAT GROWS IT IS UNDEFINED ORDER, AND GCC
TAKES THE OTHER ONE.** `kScriptNames[AddNameTableName(...)].value` loads
the table pointer BEFORE the call under GCC; the call reallocates the
table every tenth name, so a forward-referenced name that landed on a
growth boundary read its uid out of the freed block as 0, while the forty
names around it were right. MSVC calls first and loads after, which is why
the reference recorded from the original never showed it -- and why the
actions oracle would have, had anything run it since. The shape to grep
for is `[Identifier(` over anything that can `realloc`: sequence the call
into a local first.

**A `peek` OF A BARE HEX ADDRESS IS A `peek` OF 4.** `strtoul(..., 0)`
reads `4fa898` as decimal 4 and stops at the letter, and reading address 4
took both games down mid-comparison in a way that read as the verbose flag
crashing the logger. The command refuses an unreadable address now, as
`dump` always did. Write socket addresses with `0x`.

**THE CRT IS RECONSTRUCTED LIKE THE GAME, under `src/platform/crt/`, and
its names carry a `crt_` prefix.** The image's MSVC 6 runtime is 231
functions the game reaches 44 of, and glibc behind those names is a second
runtime with its own tie order, heap and float formatting -- each a frame
the hybrid and the native build can disagree on. So it is derived from
the disassembly the way `src/game` is, one function at a time, each
declaration opening with its address; `crt_` because the host's libc owns
the plain names; the game reaches them through the `ADDR_CRT_*` seams in
`standalone.h`, so nothing in `src/game` changes; and its state is the
image's, `ADDR_RAND_SEED` and the rest, so original and reconstructed code
share it. Verification is by enumerating oracle where the function is pure
over memory a stub can supply -- `tools/qsortcheck.py` runs the original's
sort under Unicorn with a comparator stub in the scratch page and replays
the recording through the C in `tests/selftest.cpp` -- and by lockstep for
the rest. A mutation that cannot fail is stated as a theorem in the tool,
not left as a gap: which side of a partition `qsort` pushes first changes
nothing but the stack depth.

**THE HYBRID IS THE CRT'S ORACLE, AND IT NEEDS NEITHER WINE NOR AN
EMULATOR.** `build/armymen2-hybrid-dev` runs the ORIGINAL CRT over
`src/platform`, so its `fopen` reaches our `CreateFileA` through the IAT
and its `fread` our `ReadFile`; link `src/platform/crt` beside it and the
reconstruction reaches the same two by name. `AM2_CRTCHECK=<dir>` takes
over at `WinMain`, after the original's startup has built the heap and the
handle tables, and `src/hybrid/crtcheck.cpp` runs one scripted sequence
of stdio calls through each stack on its own copy of a generated file,
comparing every return value, errno, the FILE's fields, a hash of the
bytes and the file left behind. Where Unicorn needed a hooked `ReadFile`
and a Python model of a file, this needs nothing: the same platform is
under both, so only the CRT is being compared. `tools/crtcheck.py` is it
in `make check`, in seconds. Reach for this shape before an emulator for
anything the CRT does through kernel32.

Two things it settled on its first run that reading had not. A field can
be STALE rather than wrong: neither `_getstream` nor `_openfile` writes
`bufsiz`, so a fresh FILE carries whatever the slot's last user left, and
the comparison reads it only while a buffer exists -- an exact oracle over
shared tables has to know which fields the API can see. And the original
has undefined behaviour the reconstruction reproduces exactly: a read on
a stream whose last operation was a write leaves `cnt` at -1 through
`_filbuf`'s error arm, after which the next `fwrite` copies
`min(remaining, 0xFFFFFFFF)` bytes into the buffer. Both stacks took the
process down on that until the scripts stopped asking; a mutation that
makes writes fail reaches it again through a failed flush. Say what a
corpus must NOT do as well as what it reaches.

**A CRT FUNCTION THAT READS A TABLE STARTUP FILLS IS RIGHT AND EMPTY IN
THE NATIVE BUILD, and the two have to be told apart when it is compared.**
`getenv` walks `_environ`, `_mbctoupper` reads `_mbctype`, `__tzset`
consults both; the original's startup fills all three and nothing in the
native build does, so there they answer as if the table were empty --
which is faithful and is not what the original answers. The hybrid is
where the comparison is exact, because its startup has run. The other
half is state the two stacks SHARE in that process: `__tzset` runs once
and `time()` caches its minute, so whichever stack runs first does the
work and the second inherits it. Reset that state before each stack, or
the second stack's computation is never compared at all -- it was not,
until the timezone globals were snapshotted after each run.

**AN ORACLE OVER SHARED STATE IS COUNTERFACTUAL OR IT IS NOTHING, and a
snapshot makes it exact.** The heap check's first version asked "free a
block with one stack, then does the other hand the same block back?" --
and failed on a correct reconstruction, because after a free the block
has merged with its neighbours and the next allocation comes from the
head of whatever list serves it; the original promises nothing of the
kind either. What the two stacks CAN be asked is the same question from
the same state: snapshot every committed page, every region's bitmaps
and the five globals, run the operation through one stack, digest, put
the snapshot back, run it through the other, digest, compare. 24,279
operations that way, and every one of thirteen mutations fails. The one
thing a snapshot cannot undo is an unmapped region, so a step that
releases one is counted and skipped, not compared. Three of the
thirteen survived the first corpus and every survival was the corpus:
a "permutation" whose product overflowed before its modulus and so left
363 blocks unfreed, and no group ever empty; a growth probe that
alternated stacks, so the original repaired the table the mutation
left short; and no free entry in an earlier region while the scan
pointer sat on a later one. A mutation that passes names the input the
corpus lacks; build that input before calling the gap a theorem.

**A TABLE THE RECONSTRUCTION WALKS NEEDS ITS SLOTS REWRITTEN, NOT ONLY
ITS TARGETS RESOLVED.** `mkglobals` had resolved all 21 C++ static
initializers by name for years and generated a function that called them,
and the initializer table's own slots in the carried `.rdata` still held
the original's jmp thunks -- which the scan could not match, since a thunk
is neither a patched address nor a seam. The moment `_cinit` was
reconstructed and walked the table as the original does, the native build
jumped to `0x00408AB0` and died. Each entry is now rewritten by position
with the name already resolved for it. The general form: a fixup pass
proves only that what it could MATCH is rewritten; anything reached by
walking a table has to be checked at the table.

**AND A BLOCK'S OWNER DECIDES, NOT ITS SIZE.** A block reallocated above
the threshold and back down stays HeapAlloc's -- the original keeps it
there -- and the check, classifying by size, freed it through both
stacks: a double free of the host's block that MALLOC_CHECK_ reported
sixty operations later as corruption in the reconstruction. Ask
`__sbh_find_block` whose it is, which is what free itself does.

**A BYTE LOOP IS NOT THE ORIGINAL'S DWORD LOOP WHERE THE INPUT IS
UNDEFINED, AND THE VECTOR SET ASKS FOR UNDEFINED INPUTS.** `strcpy` and
`strcat` were written as byte loops, correct for every well-formed call;
the original walks the source a dword at a time and writes the tail from
the dword it has already read. On `strcat(p, p)` with a one-byte string --
which the generator produces, since two pointer arguments are made equal
on purpose -- the original writes two bytes and returns, and the byte loop
reads the byte it has just stored and fills memory until the selftest
dies at `0x19191919`, the pattern byte it was copying. The loop is the
original's now, for strcpy, strcat and every idiom that shares it.

**AND THE COMMIT THAT CARRIED THAT CRASH WENT IN BECAUSE `exit 2` WAS
READ AS PROSE.** The selftest's log ended in `exit 2` and the grep for its
summary line found nothing, which is exactly what a crashed run leaves,
and the commit command ran anyway. A run's exit status is the verdict;
read it before anything else, and never commit on a summary line that
did not print.

**A MUTATION PASS THAT RESTORES WITH `git checkout` DESTROYS AN UNTRACKED
FILE'S WORK AND RESETS A TRACKED ONE'S.** Nine mutations over two new
modules, restored with `git checkout -- FILE` after each: the untracked
one was left carrying two mutations at once, and the tracked one -- whose
committed state was a stub -- was silently replaced by that stub, so the
third mutation failed to build and the fourth's anchor was gone. Restore
from a copy taken before the edit, and assert the file equals it after.

**THE CRT'S FLOAT FORMATTING IS INTEGER ARITHMETIC, AND ITS CONSTANTS ARE
THE IMAGE'S.** `%6.2f` goes through a twelve-byte long double, a five-word
schoolbook multiply that rounds half to even at a guard word, a table of
powers of ten and a digit loop that multiplies by ten and reads the top
byte -- not one x87 instruction decides a digit. So it reproduces exactly
in C, and the powers of ten, `"e+000"` and the decimal point are read out
of `.data` and `.rdata` through `AM2_IMAGE` rather than transcribed; only
the 0.1 that `$I10_OUTPUT` builds on its own stack is a literal. One
idiom cost three of 1,793 cases: `mov al,[esi]; inc esi; test al; jne`
leaves the pointer ONE PAST the terminator, so a `[esi-2]` after it is
the last character, not the one before it. Write the walk as
`while (*p++)`, not `while (*p) p++`.

**THE ORIGINAL'S CRT IMPORTS ARE THE ORIGINAL'S CRT'S.** All 56 kernel32
entries the platform did not have -- heaps, virtual memory, the
environment, std handles, file I/O, find-file, code pages, string
classification, time, exit -- are called only from above `0x00465000`, the
statically linked MSVC runtime, and never from the game. `kernel32crt.cpp`
emulates Microsoft's runtime, not 3DO's, which is why a Win98 `GetVersion`
and a Latin-1 `GetStringTypeA` are enough.

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

**UNICORN KEEPS EFLAGS BETWEEN RUNS, AND A CALL THAT FAULTS LEAVES THE
DIRECTION FLAG WHERE IT WAS.** `tools/vectors.py` reuses one instance for
every vector of every function. memcpy's backward-copy arm does `std`
before its `rep movsd`; a try with a null source took that arm and
faulted, the vector was discarded as faults are, and every call after it
ran with DF set: memcpy's own next vector recorded a forward copy of 100
bytes landing 96 bytes BELOW its destination, and TitleCaseName --
merely next in the list -- had its strlen walk backwards and 116 of its
vectors demand answers the original never gives. Nothing in either
function was wrong. The harness resets EFLAGS before every run now, and
the tell was that the failures began exactly where the new function had
been inserted in the order.

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

**`mpoptions` is clean, and the six defects that got it there are in
`docs/screen-mpoptions.md`.** It went from a process that DIED when the screen
was requested to `widgets identical (131 nodes)`, `state identical`, `log
identical (35 messages)` and 151 pixels against a budget of 300.

Four rules came out of it and are stated here because they generalise past that
screen:

- **A field our code READS and our code never WRITES is a defect, and one grep
  finds every instance on a screen.** Six of `MP_PANEL_OFF_*` were unbuilt and
  each announced itself differently -- a crash on open, an empty global, a
  crash one dereference later, four misplaced nodes, a selection that must
  start at -1. The grep found all six before any was diagnosed.
- **A never-written field does not fault where it is READ, it faults one
  dereference LATER**, so solve the fault address for a garbage base plus a
  real field: 0x280 + 0x7C is the 0x2FC that named `MP_PANEL_OFF_CHATBOX`.
- **Take a long constructor's ORDER from its field stores, not from reading it
  top to bottom.** The game box writes the script name and the map list looks
  up BY that name, so the wrong order searches for an empty string. And a child
  list is ordered by when each child was added, so a block in the right place
  with the wrong neighbours still moves four nodes.
- **A widget that is CONSTRUCTED but not STORED is invisible to every check we
  have.** It draws, it is in the child list, it appears in `ctl widgets` -- and
  the field naming it holds whatever the allocator left.
**GENERALISED TO EVERY WIDGET CONSTRUCTOR, IT FINDS DUPLICATE PREFIXES RATHER
THAN MISSING WRITES.** Run over the 19 `*_OFF_*` families that have a matching
`*Construct`, the same scan reports nine families with unwritten fields -- and
the ones worth anything are not bugs in the constructors, they are records
described by TWO prefixes at once, so the constructor writes every field under
the other spelling. `CheckBoxConstruct` uses `CHECK_OFF_*`; the painter uses
`CHECKBOX_OFF_*`; eight fields had both.

**And two of the eight CONTRADICTED each other**, which is what makes this
worth more than tidying: `CHECK_OFF_SPRITE_OFF` and `CHECKBOX_OFF_SPR_ON` are
both 0x68, and `CHECK_OFF_SPRITE_ON` and `CHECKBOX_OFF_SPR_OFF` are both
0x6C. `CheckboxPaint` settles it -- it takes 0x68 when the box is checked and
0x6C when it is not -- so the `SPR_` names are body-derived and right and the
`SPRITE_` pair was named BACKWARDS at those addresses. Collapsed onto the
painter's names, and `controls`, `audiovol` and `difficulty` stay clean.

**`checkoffsets` cannot see any of this and the ratchet still earned its
keep**: it failed the build the moment the rename produced two
`CHECKBOX_OFF_CAPTION` defines. It compares WITHIN a prefix, so it is blind to
a second prefix and sharp about a second name -- which is exactly the split
this file already describes for a NEW prefix, met from the other direction.

**AND IT HAD A SECOND BLIND SPOT, WHICH WAS THE FILE LIST.** `checkoffsets.py`
read `src/inject/orig.h` and nothing else, so the **68** `_OFF_` macros in
`src/game/**.h` were invisible to it -- including `LIST_OFF_ARG7C`, a
"constructed 0" placeholder sitting on `LIST_OFF_ARROWBAR`'s 0x7C in the SAME
family, which is the one thing the tool exists to catch. Three
`FOCUSLABEL_OFF_INK2/3/4` were the same shape against `INK_FOCUS`, `PAPER` and
`PAPER_FOCUS`.

It reads them now, and found a straight duplicate on its first run:
`VTABLE_EDIT` defined identically in `orig.h` and `widget.h`.

**A NAME THAT LIVES IN A GAME HEADER STILL BELONGS IN `orig.h` IF ANYTHING
ELSE USES IT.** Deleting `LIST_OFF_ARROWBAR` from `orig.h` in favour of the
descriptive copy broke `commmsg.cpp`, which does not include `widget.h` --
the shared header is the canonical home and the comment moves to it, not the
other way round.

**The baseline went 15 to 17 and that is COVERAGE, not decay.** Four aliases
were collapsed before the number was taken, so it would have been higher
still. Whenever this figure moves, say which of the two it is.

**THE OTHER RATCHETS WERE AUDITED FOR THE SAME HOLE AND ONLY THIS ONE HAD
IT.** `checkglobals` walks `am2.game_sources()`, `checkseams` globs
`src/game/**/*.cpp` recursively, `checkinstalled` takes both header
directories and `checksplit` reads the two halves by design -- all correct.
`checkpatches` reads `orig.h` alone and that is right rather than lucky:
**all 2,957 `ADDR_` defines are in `orig.h` and none is anywhere else**, so
its scope matches its population exactly.

Recorded because the question is cheap to ask and tedious to re-derive: the
file list of a checker is part of what it checks, and four of the five were
already right. That is the check to run on any screen whose
constructor is suspected, and it is the scoped form of the read-only-offset
idea that was rejected as a whole-tree gate.

**A READ-ONLY OFFSET LOOKED LIKE A CHEAP RATCHET AND IS NOT ONE, measured
before it was built.** `checkoffsets.py` refuses a second name on an offset;
nothing asks whether an offset we READ is ever WRITTEN, and a field with no
writer is already recorded here as dead code. Pointed at our own source that
scan finds this defect -- so it looked like the obvious next gate.

It is a noise generator, and the numbers say so: **329 of 1,106** `*_OFF_*`
macros in `src/game` are never on the left of an `=`. Most are legitimate --
we read a field the ORIGINAL's code writes, which is the normal state of a
half-reconstructed program and not a defect at all.

**And its false positives look exactly like its true one.** The same scan
flags `MP_PANEL_OFF_COLOURS` and `MP_PANEL_OFF_TEAMS`, which sit beside
`ARMY_ROWS` in the same panel and read as three instances of one bug. They are
written -- through a LOCAL taken from the macro (`colours[i] = ...`), which a
macro-level scan cannot see. Believing them would have turned one real defect
into three and sent the fix at code that is already correct.

What has signal is the SCOPED question, not the global one: for a structure
whose CONSTRUCTOR is ours, every field we read must be written by us, because
nothing else builds it. That is a claim about one structure at a time and
needs to know which those are, so it is not a cheap gate either -- but it is
the form worth reaching for by hand when a reconstructed constructor is
suspected.


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

**Jump tables and the tables they index are `docs/tables.md`, and this image
dispatches through byte tables constantly.** Six distinct failure modes have
been hit -- arms laid out in a different order from the table, several slots
sharing one arm, an arm that ENDS INSIDE another, an arm unreadable from its own
body, a table that is mostly impossible, and a table that is a FILTER rather
than a dispatch. Every one compiles, passes every static check, and is invisible
to an A/B, because each indexes *something* correctly.

**So: DUMP THE TABLE before writing anything down about the order, and for a
table of any size GENERATE the code from it rather than transcribing it.** A
hand-written mapping of eighty-one codes onto twenty-four arms put six in the
wrong arm and no amount of re-reading found it; what found it was diffing the
groupings against the image's own table programmatically. Dumping a table is the
first half of the rule and not retyping it is the second.

**And find the table that gives the numbers NAMES before reading the arms** --
the item captions turn five zero entries from numbers into the items that do not
turn the soldier to face what he is aiming at.

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

**GENERALISING THAT BUG INTO A CHECK WAS TRIED AND IS NOT WORTH BUILDING.**
The obvious next move is to find every OTHER gate where the original compares
a global against a REGISTER and our C tests the same global for null. Scanned
per function, since a linear sweep of `.text` desynchronises on data and
misses the very site that motivated it -- 8 hits swept linearly against 202
scanned properly, and 0x5122C8 absent from the first.

202 sites over 97 globals, of which 15 functions also null-test the same
global in our C. **Every one of the fifteen is correct**, and they fail for
two reasons that are both ordinary:

- MSVC compiles a null test AS a register compare. `TakeMenuRequest` opens
  `xor esi, esi` and then compares ten globals against `esi`; `HudPostUpdate`
  does the same with `edi`. Those ARE `== 0`, spelled in the register.
- `movsx edx, [esi+0x10]; cmp edx, [0x4F9FDC]` is the `army == g_defaultOwner`
  idiom, in `DamageTrooper` and `UpdateTrooperAction` both, and our C has it.

The one that looked like a signed-versus-null difference was not either:
`cmp [0x511D98], edi; jle` is `ADDR_LEVEL_ID > 1`, which is exactly what
`frame.cpp` writes.

So the discriminator is not the instruction, it is WHAT THE REGISTER HOLDS --
zero, an argument, or a field -- and answering that needs dominator-aware
dataflow, not a scan. A check without it reports fifteen correct functions,
which this file already says to suspect before believing. The real instance
was found by RUNNING the game, and that is what `movecheck.sh` now keeps.

**The save/load path is `docs/saveload.md`**, and it holds the one deliberate
deviation in this tree -- `LoadItems` unlinks objects from the cell grid before
`ItemsReset` frees them, because the original's latent use-after-free there
takes the process down on an ordinary LOAD GAME. Three rules came out of it:

- **A latent defect is invisible until EVERY path that reaches it works.**
  Fixing one thing routinely makes the next thing fail, and a suite that goes
  red after a correct change is not evidence the change was wrong. Two fixes
  landing in one session uncovered a third defect neither could have shown
  alone.
- **`WINEDBG=-all` discards the page fault**, which `make run` sets, so a run
  that dies with nothing in its own log says only "detached". Launch the binary
  outside the harness before instrumenting anything -- one command gave the
  faulting instruction outright after five runs had produced nothing.
- **Static comparison can be EXHAUSTED, and saying so is a result.** Thirteen
  functions on that chain were compared against the image and all thirteen were
  faithful, which is what said the defect was not a mis-transcribed instruction
  and that reading more of it was not the way in.

**`tools/saquit.sh` COVERS THE STANDALONE'S TEARDOWN, which nothing did.**
`samenu.sh` compares a title screen and `samission.sh` a live mission's object
table, and both KILL the process -- so the comm shutdown, the sprite frees and
the leak report never ran in the standalone even once. `ab.sh quit` exists for
exactly that reason on the injected side and found a real bug the first time
it ran.

There is more at stake here than on that side: the standalone's C++ static
initializers run from OUR runtime rather than the MSVC CRT's, so anything they
registered comes down here or not at all. Measured: the game's own messages
are IDENTICAL through a full menu quit -- 7 of them -- and the standalone
reports `Unreleased memory (0) blocks`.

The harness lines are filtered rather than diffed, because the injected build
logs an attach banner, a patch list and the DirectInput hook that the
standalone cannot produce by construction. The two racing thread lines are
compared as a SET and flagged rather than failed, for the reason `ab.sh quit`
already records: one of them can be ABSENT, not merely out of order.

Mutation-checked in both directions, since a new test that cannot fail has not
passed: dropping one game line from the standalone side names the missing
lines and fails, and asking for a heap report that is not there fails
separately.

**`AM2_NOPATCH_NAMES` BISECTS A BEHAVIOUR, AND THE UNIT OF BISECTION IS A
CHAIN, NOT A FUNCTION.** It leaves the named reconstructions original, and
a reconstruction reached by NAME from another reconstruction is not reached
at all -- the caller compiled a direct call to ours. So a set that restores
the original's behaviour has to run from something reached by ADDRESS (in
the injected build that is `WinMain`, which the CRT calls) down to the
defect, and halves of the patch list fail on both sides while the whole
half passes. That is what "trial(822) -> 1, trial(411) -> 0, trial(411) ->
0" means, and delta-debugging over 821 names would have taken an hour to
say it. Write the by-name chain down from the call sites, confirm it as a
set, then drop members greedily: what survives is the path to the defect,
and the deepest survivor is the function to read. Sarge walking through
Boot Camp's hut took eight names and twenty-two trials that way, and the
defect was three misreadings in one function, none of them an offset.

**THE NATIVE BUILD IS TWO BINARIES, and the one a drive talks to is
`build/armymen2-dev`.** `make native` builds both. The player's has no control socket at all; the
development one adds the socket (on by default), the
injected input, and savestates over a fixed-address arena
(`snap save|load FILE`); F5 writes the GAME's save as a fixture and F9
reloads it. Anything that pokes, dumps or drives the
native game uses the dev binary; STATUS.md has the measurements.
`tools/savecheck.sh` is the cross-build serialisation A/B built on it --
the game's SAVE GAME written by one build and loaded by the other, judged on
the object tables, because the file's bytes carry raw pointers by design.
**`tools/enterlevel.sh [-f FOLDER] SAVE` enters a mission from a save and
leaves it FROZEN before its first frame, on any of the three builds, in
fifteen seconds and without a menu**: the socket's `loadgame` makes the LOAD
button's four writes and `AM2_PAUSE_ON_ENTER=1` freezes the arrival. One
file gives one table md5 across runs and across builds, Boot Camp included
-- it saves once the game-proc block's first string holds a folder name,
which in Boot Camp is empty (no player), so the dev binary's F5 fills it
with the level's name; the level name is the string 0x20 further in, and
reading that one as the folder is how this was first got wrong. Reach for it before hand-driving to a point in a
mission: it is a fixed point, and a drive is not.

**Movement and the input path are `docs/movement.md`.** Two defects a month
apart each made the player unable to move and neither was visible to any A/B --
both sides are driven with the same input and agree about ignoring it.
`tools/movecheck.sh` is the configuration that catches the class, and the only
one in the project that drives a REAL device: `xdotool` events go through Wine's
own DirectInput, where the socket's `cursor` and `key` write the game's globals
and exercise neither poller. Its assertion is DISPLACEMENT, not equality.

**A gate whose operand is a REGISTER cannot be checked by reading the global.**
`ADDR_OBJ_CTX_OBJ_A` is non-zero in a live mission on both sides, so reading it
proved nothing until the instruction was decoded -- and `checkoffsetuse` cannot
see it either, because every offset involved is correct.

**The combat layer is `docs/combat.md`, and NO configuration here reaches it.**
One `counts` line settles what used to be a dozen separate mysteries: on a
driven Boot Camp mission with the frame ticking, `ShotStrike`,
`ApplyShotDamage`, `DamageObject`, `CreateMissile`, `FireWeaponAtPoint` and
`FireWeaponAtObject` all read 0. So the whole layer is checked by the oracles in
`docs/oracles.md` or not at all.

**A HOT function can still be undiscriminated**, which is the finding worth
carrying out of it. `ShotStrike` runs 13,582 times on that drive and the suite
cannot see a mutation to its penetration branch, because almost every one of
those calls finds nothing at the point and falls straight through. **Ask which
BRANCH the calls take, not how many there are** -- a five-figure counter can
certify only the path that does nothing.

**And the suite cannot see MOVEMENT at all**, measured categorically rather than
inferred: `MoveStepPoint` runs 175,145 times and adding a flat 50 to every step
leaves `combat` clean. Every configuration reaching live play has its pixel
check disabled by construction, and nothing else it compares mentions a
position. `tools/objdump.py --table` closes half of that -- it diffs all 1,609
objects exactly, at the BRIEFING, so it covers what the map load builds and not
what happens once the mission starts.

**How to read an A/B result is `docs/abnotes.md`.** Two of the four artifacts
are noisy by construction and most of the time lost to this suite has gone on
reading a noisy number as a result. The five rules:

- **`mission` and `combat` have their pixel checks disabled by construction** --
  two unsynchronised runs of a live scrolling mission differ by a quarter of the
  frame either way. The log is the evidence there.
- **The `frames` line is `orig/recon`.** Read WHICH SIDE moved before the ratio,
  and read it out of the named `.volatile` artifacts rather than remembering the
  argument order -- that was got backwards within a day of being written down.
- **Check `uptime` before believing a failure**, and reach for the PARENT COMMIT
  rather than a fourth re-run.
- **Give the control as many samples as the thing it controls for, in BOTH
  directions.** One clean control does not establish a regression; one dirty
  control does not establish one either.
- **Ask whether the new code EXECUTES before comparing runs of it.** A counter
  of 0 answers "could this possibly be the cause" outright and needs no samples.

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

**`AM2_WINE_OUT` KEEPS WINE'S STDERR, and without it a fault is invisible.**
`drive.sh` sent the launcher's output to `/dev/null` -- the right default,
since it is thousands of `fixme` lines -- and that also discarded the only
record of a page fault. A game that dies mid-drive writes NOTHING to its own
log, because the log is our harness's and a fault never reaches it, so the
symptom is a clean `am2hook detaching` and no explanation. Set
`AM2_WINE_OUT=<file>` and pass `WINEDBG=+seh` to get the exception code and
address; that pair is what identified the `mpoptions` fault after five runs
had produced nothing but "it exits". Reach for it whenever a run ends with no
game log line to blame.

**THE STANDALONE NEEDS AN ABSOLUTE WINDOWS PATH UNDER `explorer`, and the two
ways of getting that wrong fail differently.** Verified today with the whole
session's work in: its log is the same four lines the commit that first
reached the menu quotes -- `system speed`, `Using High Performance Counter`,
`Lobby start`, `Releasing Comm Connection` -- and the frame renders 234
distinct colours rather than a flat desktop.

    wine explorer /desktop=NAME,1024x768 "C:\GOG Games\Army Men II\am2port.exe" -nointro

Run with a RELATIVE path under `explorer` it exits silently: no log, no
window, nothing on wine's stderr, which reads exactly like a broken build.
Run WITHOUT `explorer` it gets four lines further and dies at
`DDERROR 80004001: InitDirectDraw` -- E_NOTIMPL, because there is no virtual
desktop for the mode change and Xvfb refuses it. Neither failure says
"invocation"; both say "the port is broken". Three attempts were spent on
that before the path was the thing that changed.

Install it as `am2port.exe` beside the original rather than over it. The
original is needed at BUILD time and by every A/B, and `make standalone`'s own
message -- "copy it into the game folder to replace ArmyMen2.exe" -- is about
what a player would do, not what this tree should.

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

**AND THE CURE FOR THAT -- MATCHING ON SOMETHING BESIDES THE EXE NAME -- HAS
ITS OWN FAILURE, WHICH COST HALF A SESSION QUIETLY.** A `/proc` sweep that
excludes the caller's own ancestor chain is the right shape, and the one used
here then narrowed on `"wine" in cmdline or "explorer" in cmdline` to avoid
matching shells. The game's own processes do NOT say either:

    C:\GOG Games\Army Men II\launcher.exe C:\GOG Games\Army Men II\ArmyMen2.exe

so every "cleaned N" that sweep printed was counting the wrapper and leaving
the GAME running. Two instances survived a dozen kills, kept
`ArmyMenMutex` and port 31436, and the next suite failed with `bootcamp/orig
produced no game log lines` -- the exact symptom this file already documents,
diagnosed at once and caused by the tool that was supposed to prevent it.

**The EXE NAME is the signal; the ancestor chain is what excludes the caller.**
Do not add a second predicate about how the process was launched, because the
launcher's command line is the game's own path and says nothing about wine.
And check BOTH conditions afterwards -- `ss -ltn | grep 31436` as well as the
process count -- which this file already says and which a "killed 0" makes
very easy to skip.

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

**IT HAPPENED A FOURTH AND FIFTH TIME IN ONE HOUR, so the mechanism is a
tool now: `tools/pidfd.py wait PID [TIMEOUT]` and `tools/pidfd.py kill PID
[SIG]`.** A waiter on `pgrep -f 'timeout 900 make check'` spun after the
check had passed, and the `pkill -f` meant to end it killed the shell
issuing it -- one command before `git commit`, which therefore never ran. A
pidfd names one process for as long as it is open: it becomes readable when
that process exits and a signal through it cannot reach a reused pid.
Capture `$!` when a job starts and use nothing else to wait for it.

**AND `$!` IS NOT ALWAYS THE PROGRAM, so `kill` signals the descendants
too.** Two `strace` runs from a leak hunt were killed through the tool and
WAITED FOR, and the wait returned -- because the pid was a wrapper. The
tracers were reparented to init with their games and ran for three and a
half hours: 1.9 GB resident each from the very leak being hunted, and two
UNLINKED trace files of 9 GB and 2.8 GB held open on a tmpfs /tmp, which
the user found at 16 GB after resizing it. `du` could not see them, since
the names were gone; what did was walking `/proc/*/fd` for links ending
`(deleted)`. `pidfd.py kill` walks the pid's descendants deepest first
now and prints each one it signals, and takes `KILL` as well as
`SIGKILL`, which it did not. After any kill, read `df` and the deleted-fd
walk, not only the process list: a dead name can still be a live file.

## Open items

- **The Lock/Unlock bracket goal is CLOSED** -- 29 functions call the bracket
  and 29 are ours. See STATUS.md for where it stands and for the queue that
  kept being wrong about it. Two things from it are durable and stay here:

  Do not hand-edit that pair. `tools/checkclaims.py` recomputes it, and it is
  the reason this sentence is right: the count moved from 10 to 11 the moment
  `TyperPaint` was written, and the check failed the build rather than letting
  the prose go quietly stale — which is the whole argument for the tool.

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
  **Generate the queue, do not write it down.** That shortlist was wrong in
  both directions more than once -- naming functions that were already
  reconstructed, and giving a count that disagreed with its own table, in a
  paragraph whose whole argument was that queues should be generated. A count
  that only goes up cannot tell you a candidate has been taken; only re-reading
  the list against the patch list can, and nobody does that. The history is in
  STATUS.md.

  Worth being clear about what this goal was: the bracket finds the game's
  software RASTERISERS, which is a rewrite goal of its own. It is not the
  Win32/DirectX boundary, and the pairing is per FEATURE rather than per
  function -- both line drawers Lock and never Unlock, so a count of "functions
  calling the bracket" keeps finding halves.
  actually execute is inside reconstructed code or incidental, and all 207
  confirmed COM dispatch sites below the CRT are ours. **Read the figures from
  `docs/boundary.md`, never from prose here** -- quoting a generated number in
  prose is how three separate figures in this file went stale, and one of them
  was in this very bullet.

  Four things from it that change how you WORK rather than what you know:

  - **A vtable call is only COM if `this` is PUSHED.** An i386 MSVC C++ virtual
    is thiscall and keeps `this` in `ecx`; both compile to the same
    `mov vt,[obj]` / `call [vt+N]`. Whichever appears CLOSEST to the call wins.
    145 of 353 in-game sites are C++ rather than COM, and a survey matching on
    shape alone reports the engine's own destructor chains as DirectX.
  - **Creating or destroying an OS object is boundary work; operating on a
    handle you were given is not.** That line has to be the same for kernel
    objects as for COM or the word stops meaning anything.
  - **Pick the next target by boundary density, not by import count** -- but the
    denominator is wrong for one entry in eight, because `functions.tsv` runs
    neighbours together where it cannot see a boundary. Run `tools/merges.py`
    before ranking anything.
  - **A tool that recommends targets has to know what is already DONE**, and
    three separate ways of not knowing bit within an hour: it ranked out of a
    description of the ORIGINAL image, it let unclassified sites through as if
    unknown meant COM, and not every reconstruction is a `patch_replace`.

  **A function declined on density can still arrive because the layer around it
  did**, which is what happened to all three of the last declines. A density
  ranking does not decide what gets reconstructed, only what gets reconstructed
  FIRST.
- **`tools/samission.sh` IS THE SAME COMPARISON IN PLAY**, and it is the
strongest check the port has. It drives both builds to the same point of the
same Boot Camp mission -- past the briefing and the instruction sign, into
sub-state 0x21 -- and diffs the whole object table with NO budget: 1,609
objects, each with type, flags, army, position, tile, both rectangles,
health, cell count, AI mode and pose. It reads IDENTICAL at 1,610 lines.

Two things make it work where four rounds of hand-driving did not. The cursor
is placed through the CONTROL SOCKET, which writes the game's own three
globals, so both builds take identical coordinates -- a relative move lands
somewhere else, because Wine's acceleration is non-linear. And the button is
still xdotool's on both sides, because the standalone has no DirectInput hook
and the socket's `mouse` is inert there; using the same real button keeps the
two drives the same.

**MUTATION-CHECKED, AND THE MUTATION HAS TO BE STANDALONE-ONLY.** Both halves
build from one tree, so an ordinary edit changes them together and the diff
stays empty -- the corpus-derived-from-the-model trap in a new shape. Guarded
with `#ifdef AM2_STANDALONE`, adding 1 to SetMaxHealth's argument fails the
run and names the objects and the field: health 60 against 62, 138 against
140. Restored, it reads identical again.

It also refuses to pass on nothing: a dump under 100 lines is a VOID that
names ArmyMenMutex, because two empty tables diff as identical and that is
how three wrong conclusions were reached in one session.

**THE PORT'S LIVE OBJECT STATE IS IDENTICAL TO THE ORIGINAL'S, all 1,610
lines of it**, and that is what the control socket was added to find out.
Driven to the same point of the same Boot Camp mission -- past both dialogs,
sub-state 0x21, the clock running -- `tools/objdump.py --table` gives the two
builds the same 1,609 objects with the same type, flags, army, position,
tile, both rectangles, health, cell count, AI mode and pose. Not a budget, not
a pixel count: a diff with no slack that comes back empty.

That is a far stronger statement than `tools/samenu.sh`'s title screen,
because it is taken in PLAY rather than at a static menu, and it is the
artifact `ab.sh bootcamp` already treats as its sharpest -- the one that has
caught a wrong field five times where the pixels and the log agreed.

**AND IT SETTLES A BUG REPORT THAT FOUR ROUNDS OF PROBING COULD NOT.** The
port was reported unable to move Sarge. With the cursor placed ABSOLUTELY
through the socket rather than by relative motion -- which Wine's
acceleration makes land somewhere else, and which is why every earlier
attempt was inconclusive -- a click at the same point moves Sarge in NEITHER
build: `pos=1743,1052` before and after, on both. So the click is not a move
order, the port is not diverging, and the thing to fix is the drive rather
than the reconstruction.

The general shape is one this file already states and I still had to learn
again here: **a bug report about the port needs the ORIGINAL measured the
same way, first, with an artifact that cannot pass on nothing.** Two of the
comparisons on the way to this one came back "equal" because both sides were
EMPTY -- an objdump against the wrong port, and an A/B whose drive never
reached the mission.

**AN A/B CAN PASS ON AN EMPTY STATE DUMP, and it did.** `tools/ab.sh`
compares the object table only `if [ -s ... ]` on both sides, so a run where
the drive never reached the mission produced two EMPTY dumps, skipped the
comparison silently, and reported **A/B clean** on a four-line log and 0
differing pixels. The cause was three leftover standalone processes holding
`ArmyMenMutex` -- `tools/samenu.sh` was killing the `wine explorer` wrapper
by pid and not the game beneath it -- so both halves exited early and
compared nothing.

This is the sibling of the missing-file case this file already records, where
"two missing files diff as identical". Empty ones do too, and the `-s` guard
that fixed the first hides the second. The configurations that INTEND a dump
now leave a marker beside it, and an empty dump with a marker is a VOID that
fails the run and names the likely cause. Tested both ways: with a decoy
holding the mutex it fails and says so, and without one it reads the usual
1,610 lines, 13 messages and 22 pixels.

The reading to take is the one this file keeps arriving at from new
directions: **a clean verdict on a configuration whose evidence is missing is
worse than no run**, and the artifact counts are what tell the two apart.

**`tools/samenu.sh` IS THE STANDALONE BUILD'S A/B, and it is the only place
the port's headline claim is checked rather than looked at.** It runs the
injected build and the standalone through the same startup, then compares the
two things that CAN be compared: the game's own log lines, which come out
identical at five messages, and the title screen, which is static on both
sides. Measured: **0 of 307,200 pixels** on a run where the cursor happens to
land in the same place, 45 when it does not.

It is deliberately not a configuration of `tools/ab.sh`. That compares one
binary with and without our patches; this compares two DIFFERENT executables,
and wiring it in would have meant teaching every stage about a second one.

Tested in the failing direction, which took three tries and each failure was
the SCRIPT rather than the port:

- `set -e` aborts on a failing last command in an `&&` list, so a `kill` of a
  process that had already exited took the script down -- after it had
  printed that it PASSED, leaving a passing run reporting failure.
- doing that kill BEFORE the comparison ended the script silently, which read
  exactly like the budget check failing.
- `set -e` also aborts on a failing command substitution, so `SA_PID=$(cat
  ...)` on a pid file that had not been written yet exited before anything
  was compared.

A budget of 0 is NOT a failing-direction test here, because the cursor lands
in the same place often enough that a clean run really does read 0; `-1` is,
and that is what proves the check can fail.

**A PROBE INSIDE THE FRAME LOOP IS MEASURING THE COMPILER UNLESS IT IS
`volatile`, AND I MADE THIS MISTAKE TWICE IN ONE SESSION BEFORE CHECKING.**
Chasing a report that the standalone port would not move Sarge, a probe in
WinMain's `for (;;)` read `ADDR_GAME_CLOCK_MS` and reported it frozen at 100
forever -- which reads as the whole answer, since nothing that depends on
elapsed time can run. A second probe read `ADDR_MENU_MODE` and reported the
sub-state stuck at 24, an in-mission dialog arm rather than play. Both were
plain `*(uint32_t *)(uintptr_t)ADDR_X` reads; neither address is written by
anything the compiler can see from that loop, so GCC hoisted the load out and
re-reported one stale value. With `volatile` the clock climbs normally and
the sub-state reaches 33.

The tell was available and ignored: `FrameClockStep`'s own probe showed the
clock at 19,462 ms in the same run the loop probe called it 100. Two
measurements of one global disagreeing means one of the measurements is
wrong, and the one to suspect is the one in the hot loop.

**AND THE CONTROL SETTLED IT WHERE FOUR PROBES DID NOT.** Running the SAME
drive and the SAME measurements against the injected build gave submode 33
against 33, pause 0 against 0, a clock advancing on both, 104 pixels against
117 for a held arrow key, and 248 against 0 for a click-to-move. Holding a
key does not scroll in the ORIGINAL under this environment either. So there
was no divergence to find, and every hour after the first probe was spent
looking for one. This file already says to give the control as many samples
as the thing it is controlling for; the corollary is that a bug report about
a port needs the ORIGINAL measured the same way FIRST, before anything is
instrumented.

**A PROBE THAT REDEFINES THE FUNCTION'S NAME ALSO REDEFINES ITS PATCH.** The
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
- **94% OF THE CARRIED BLOB IS ZERO-FILL, and emitting it cost 1.84 MB of the
binary.** The standalone places the original's `.rdata` and `.data` at the
addresses their own pointers were written for, spanning 0x0046F000..0x00666000
-- 1.96 MB. Measured: only 73,772 bytes of it are non-zero and the last one is
at 0x0048D8D3, because MSVC folds `.bss` into `.data` and the original's file
never contained those bytes either.

So the span is split at the next page, 0x0048E000: `.origdat` keeps the
initialised head and an ALLOC-only `.origbss` covers the rest, which the
loader zeroes. `build/ArmyMen2.exe` went from 6,102,591 bytes to 4,169,345,
and the split address is written by `mkglobals.py` into a file the link line
reads, so the section start and the split cannot disagree.

**`.origgap` is NOT eligible and the contrast is the point.** Its content must
be 0xCC, because zeros there decode as `add [eax], al` and SLIDE -- a call to
a missing seam then faults at an address unrelated to the call, which is how a
missing `free` reported itself at a value appearing nowhere in the binary. A
region of zeros and a region that is uninitialised are the same thing only
when nothing executes it.

The evidence it is behaviour-free is `samission.sh`: the object tables live
ABOVE the split, at 0x0065xxxx, and 1,610 objects come back identical -- so
the region is demonstrably both writable and zeroed. The pixels and the log
could not have shown that; only the artifact that reads those addresses does.

**`tools/checkgap.py` guards the standalone's own version of that failure.**
The standalone does not carry the original's `.text`; that range is int3, so
a reference into it is not a link error and not a crash at the call site --
it is whatever the filler means. `c_dfDIMouse` is how this was found: the
DIDATAFORMAT struct is in `.rdata` and IS carried, its `rgodf` array is at
`0x004643A0` and is NOT, and the game reported "DDERROR 80070057:
SetDataFormat (mouse)" -- a plausible DirectInput failure with nothing in it
about a missing byte range. A missing `free` seam did worse and faulted at an
address that appears nowhere in the binary.

So: any `ADDR_` macro `src/game` DEREFERENCES whose address is in the gap
must be redefined under `AM2_STANDALONE`. It is **37 of 37** today, all of
them the statically linked MSVC CRT plus the stubbed logger -- which is
exactly the set the port points at the real C library. Tested by removing one
definition, which names it and its use site.

It skips `patch_replace` targets, since installing a patch in the standalone
is a no-op and the address is never reached; and it resolves macros rather
than dataflow, so a seam reached through a variable is invisible, the same
blind spot `checkseams.py` records.

**And it disproved a hypothesis cheaply, which is most of what it is for.**
Movement not working in the standalone looked like a missing input seam --
the mouse data format had already been exactly that. One run said 37 of 37
were covered, so it is not a gap read, and no time was spent reading the
input path again.

**`tools/checkhooks.py` guards the one failure that no A/B can see.** It
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
- **What no drive reaches, and what windowed mode looks like today, is in
  STATUS.md.** Both are state rather than policy: windowed mode paints now
  where it used to stay black, so "it is black" can no longer be quoted as the
  reason a windowed comparison is trivially exact; both DirectDraw `Restore`
  paths are still untested, and `LockSurface`'s is a real defect in the
  ORIGINAL kept as-is deliberately; and no configuration reaches combat, which
  `docs/combat.md` covers.
- **What no drive reaches, and what checks it anyway, is `docs/oracles.md`.**
  Thirty-one of the thirty-two unexercised functions are checked by ENUMERATING
  ORACLES -- tools that build a corpus, run the original under Unicorn, and
  compare -- because no configuration here provokes a fight, opens a DirectPlay
  session or reaches an in-mission dialog. `tools/checkclaims.py` recomputes
  that split from the tools, so it cannot go stale.

  Four rules from it that apply to any new oracle:

  - **A counter of 0 usually means nothing.** `tools/blindspots.py` says which
    counters CAN move: of 1,640 traced functions, 54 mean what they say, 1,346
    are blind because every caller is ours, and 240 are reached by address. Ask
    it before reading a zero as evidence, and note it gets worse as the
    reconstruction gets better.
  - **A corpus derived from the model under test cannot fail against it.** Four
    instances now. The tell is that the CASE COUNT moves under a mutation
    rather than the verdict -- a mutation that shrinks the corpus reads exactly
    like one that was absorbed.
  - **Ask what a function's output actually IS before comparing anything.**
    Several of these answer the same value on every path and do their work by
    WRITING a field, so an oracle comparing `eax` would pass with the body
    deleted. Seed the slot with a sentinel, so "wrote nothing" is
    distinguishable from "wrote zero".
  - **Say which of a tool's gaps are gaps and which are THEOREMS.** Three
    mutations here provably cannot be caught by any corpus -- a continuous
    piecewise ramp, a 16-bit tile index, an always-zero table bit -- and that
    is a different statement from "not covered".
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
- **The object types are `docs/objects.md`** -- seven of the eight identified,
  none of them guessed. Two rules from settling them:

  **Reach for `LoadTypeN` when a type is unidentified.** The per-type savegame
  loader is where a type's constants all appear at once -- the box, the row
  spec, the anim table and the def record -- which is how the missile and the
  roach were settled.

  **A shared teardown arm names nothing.** Four types share one `FreeItem` arm
  and three of the four were identified from elsewhere, which is the same rule
  as "a `free` is the weakest possible toucher", one level up.

  Worth noting HOW type 2 stayed open: nothing was missing. `DestroyTrooper`
  had been named from its own log string and `FreeItem`'s switch had been
  reconstructed with all its arms, and this list went on saying unidentified
  because nobody put the switch beside the question. **Before recording
  something as unknown, grep the tree for what already answers it.**
