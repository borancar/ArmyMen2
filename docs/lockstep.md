# Frame-exact comparison: hybrid, lockstep, side-by-side, heap

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
The stretch rounding was a real finding on the way -- for a while the platform
stepped wined3d's truncated 16.16 increment to match that Wine frame -- but it
was not the cause. (And the truncation turned out to be Wine's artifact, not
the game's: it made the pointer's 12x16 -> 32x32 -> 12x16 save/restore round
trip land a pixel left of where it came from, the residue that smears on
non-repainting screens. am2_stretch samples pixel centres now, which makes the
round trip exact -- see its comment and STATUS's 2026-09-16 entry.)

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

**A HEAP ADDRESS IS PART OF THE GAME'S STATE, SO THE PLATFORM'S HEAP IS
FIXED.** The depth comparator (`0x0041D740`) breaks an exact tie between
two objects by comparing their POINTERS, so which of two overlapping
sandbags is drawn last -- and one corner pixel -- is decided by where
malloc put them. Faithfully reproduced on both sides, the hybrid and the
port still differed: the original CRT's `HeapAlloc` reached glibc and the
port's reached the dev arena. `src/platform/fixedheap.cpp` is one heap
for both, at fixed addresses, under `HeapAlloc` and `VirtualAlloc`'s
reservations; `AM2_FIXED_HEAP=0` bisects it. `sessions/corner-1276.txt`
is the replay, identical since. The reading to carry: when both
transcriptions are right and the frames still differ, the difference is
below the game, and the platform is where every remaining source of
nondeterminism has turned out to live.

**A BLIT-INVOCATION TRACE DIFFS THE TWO GAMES' DRAW CALLS, and its first
run said "same parameters, different ORDER".** `AM2_TRACE_BLIT=1` logs
every blit (name, pump, x, y, source rect) on both sides: the port's
`blit_core` prints it directly, and the hybrid gets a TRAMPOLINE DETOUR
over each original blit entry (`src/hybrid/loader.cpp`) -- the five blits
share one 6-byte prologue and pass their rect as an `AM2_Rect` by value,
which flattens to the same __fastcall stack as four ints, so one signature
serves the log hook and the trampoline that runs the original body. The
raw data pointer is left out because it is a heap address. Pointed at the
bottom-edge divergence it showed two overlapping sprites drawn in opposite
order with byte-identical parameters, so the cause is DRAW ORDER, not the
blitter -- which with an identical heap means the depth key, i.e. the same
item-height divergence STATUS.md already tracks. A parameter trace answers
"is it the caller, the callee, or the order" before a single blit body is
read; reach for it when two builds' pixels differ but the object tables do
not.

**A `continue` THAT SKIPS A SHARED TAIL IS A DROPPED CALL, and it cost
every map item its height.** `BuildMapObjects` has an item arm and a
weapon arm; the original's item arm (0x0042D1C3) JMPs into the SAME tail
the weapon arm falls through to, and that tail is the
`ApplyHeightItem(obj, tileAttr + elev)` at 0x0042D30B. The reconstruction
ended the item arm with `continue`, so map items kept height 0 instead of
the terrain's -- which halved their depth layer and drew overlapping
ground sprites in the wrong ORDER, the bottom-edge divergence. Two traces
found it without reading a blit body: `AM2_TRACE_BLIT` said the params
matched but the order did not, and `AM2_TRACE_HEIGHT` -- the port's
`ApplyObjHeight`/`ApplyHeightItem` logged in item.cpp, the hybrid's the
same two through a trampoline detour in the loader -- showed the hybrid
calling `ApplyHeightItem hin=50` for the items where the port called
nothing. The lesson is the "An exit NOTED is not an exit reproduced" one
in reverse: when the original's arms CONVERGE on a shared tail, a
per-arm `continue` silently drops it. Read where each arm's jump LANDS,
not just what it does.

**THE HEAP'S LAYOUT IS THE SEQUENCE OF CALLS, AND FOUR THINGS OUTSIDE
THE GAME WERE IN THAT SEQUENCE.** With one fixed heap under both games the
trees at pump 120685 still sat at different addresses, and the port's and
the hybrid's object tables were IDENTICAL while their pointers were not
-- the table prints values on purpose, so the one thing that differed was
the one thing it cannot show. `AM2_TRACE_HEAP=1` logs every `_nh_malloc`,
`free` and `realloc` with the pump, the thread and the CALL SITE, and the
hybrid installs the reconstruction's three over the original's entries so
both sides log the same code; `diff` on the two logs, identities rather
than addresses, named each cause in one run. None was in the game: the
MSVC startup copies the ENVIRONMENT and the MODULE NAME into the heap and
the platform was handing each process its own (both canonical now); the
port's CRT parsed argv before `main()` had set the command line, so it saw
one argument where the hybrid saw three (the constructor reads
`/proc/self/cmdline`); and the shipped image's patch that hides
MULTI-PLAYER turns a `je` into a `jmp` AFTER the `new`, so the original
still allocates the button it never shows, and our loop, skipping the row,
did not. A `diff` of the sequence is a tool the game cannot lie to; reach
for it the moment two builds' pointers differ, before reading a single
comparator.

**AND ONCE THE HEAP IS IDENTICAL, A FRAME THAT STILL DIFFERS IS THE
GAME.** `input-play14.txt` traps at pump 120685 with 30,359 heap
operations agreeing exactly on both sides, so the 300 pixels there are a
real reconstruction divergence, not a pointer tie-break: one item's
`OBJ_OFF_HEIGHT_SET` reads 50 against 0, which halves its depth layer.
The heap trace's whole value is that it takes the heap OFF the table of
suspects -- when it comes back clean, the difference is above it, and the
depth trace (`AM2_TRACE_DEPTH`) reads the field out. Open in STATUS.md.
