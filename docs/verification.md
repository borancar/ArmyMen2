# Verifying a reconstruction

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


## Drive configurations and their lessons (bootcamp/mission/audio/intro/multi/df/quit)

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
