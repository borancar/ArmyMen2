# Status

A dated snapshot of where the reconstruction is, kept so a session does not
have to re-derive it. **`CLAUDE.md` and `docs/` are authoritative**; this file
is a summary and can be stale between updates. Every number below carries the
command that produces it, so it can be re-measured rather than believed.

Last updated: **2026-09-03**, at `1bc33e8`. Working tree clean.

## In flight

Nothing uncommitted. **1,484 patches plus 4 REGISTERED**, **30** analysis
tools in `make check` (`tools/checkpatches.py`; `make check` for the tools).

## ONE FUNCTION LEFT

`FireWeapon` (0x0045F460, 3,200 bytes) is the only game function in this image
that is not reconstructed. Everything below the nominal CRT line was closed
first; everything above it went since.

Its dispatch table is already decoded and recorded in `orig.h`, which is the
expensive half: a 43-entry byte index at 0x004600B0 into 24 arms at
0x00460050, with THREE arms shared and a default that fifteen kinds reach
explicitly. Numbering the arms as they are laid out gives 24 behaviours for 43
kinds and puts most of them in the wrong place, so start from the table.

1,149 instructions, 25 returns, and all 25 callees are named -- so the cost is
the arms, not the callees. It is a session's work on its own and should have
one; a half-written table-driven function is what CLAUDE.md warns produces
defects nothing here can see.

## Where the boundary is

The sub-CRT set is **closed at 1,239 of 1,239** and has been since this
morning. Work is above the nominal `CRT_START`, where `tools/crt.py` measures
**112 game functions** between `0x0045C000` and the real CRT frontier at
`0x00464420` -- code every count in this project silently dropped until that
tool existed. **80 of the 112 are done, 32 remain, 12,714 bytes**
(`tools/crt.py` then subtract the patch list; the recipe is in this file's
git history).

Largest outstanding: `FireWeapon` (0x0045F460, 3200 B), `MissileDefFind`
(0x004602C0, 1296 B), `PausedFrameStep` (0x00462600, 1088 B).

## Families closed today

**The vehicle step, five of five.** `Step3TurnBlocked`, `Step3ChooseFacing`,
`Step3TurnState`, `Step3RouteAndBoard`, `Step3Drive` (0x0045CB30, 769
instructions) and `Step3Input` (0x0045C050) -- input, turn planning, routing,
boarding and the drive itself. **Every one is COLD**: no configuration here
puts a player in a vehicle, so the transcription checks are the verification
and the A/B only says nothing else broke.

**The vehicle delta protocol, both ends.** `VehicleUpdateAppend` (0x0045DAA0)
and `VehicleUpdateApply` (0x0045DF10), plus the batch walk `RecvVehicle1B`
that drives the second, and `RecvVehicle1E` and `RecvVehicle24` beside them.
Needs a live DirectPlay session, so also verified by reading.

## What the checks caught today, which reading did not

Worth keeping as evidence about where the defects actually are:

- **`AppendTroopState` counted the old length twice.** Found by reading the
  vehicle sibling, not by any test -- its only caller needs DirectPlay, so it
  has never executed here.
- **Two globals were named from a caller's interpretation.** `ADDR_DRAG_ACTIVE`
  and `ADDR_CLICK_ENABLED` are mouse button 1's state and edge; our own
  `PollMouse` already spelled them as `g_mouseButton[n]`, so the tree
  contradicted itself and nothing could see it.
- **`checkseams` fired in three consecutive commits**, each time on a seam
  that became a lie the moment an address became ours.
- **`checkoffsets` refused a duplicate string define**; the compiler refused a
  duplicate `VehicleMsgRecv` I wrote without grepping the address first.

## OPEN: are the row-pool evictors reachable at all?

`RowPoolARelease`/`BRelease` and `RowPoolAEvict`/`BEvict` are installed and
**unverified**, because each has exactly one caller and the chain is

    alloc -> (only if count > capacity) -> evict -> release

with capacity 450 for pool A and 90 for pool B. If no drive fills a pool,
those four are verified by reading alone and every clean `bootcamp` is silent
about them.

A `peek` probe was written to settle it and **it failed, honestly**: all six
samples read 0 for both pool counts -- and 0 for `ADDR_GAME_CLOCK_MS` beside
them, with `ComposeFrame=0` at the end. The clock ticks during play, so the
zeroes are a drive that never reached the mission, not pools that stayed
empty. Second hand-rolled drive to fail that way today; `ab.sh`'s mission
sequence is elaborate for reasons and reinventing it in ten lines of `sh` does
not work.

The liveness reading is the only thing that made the run interpretable. Take
one whenever the conclusion is a claim about zeroes.

## Above the line: the sprite family, complete at eighteen

`0x00462A60..0x00463383` -- two dispatchers over eight subsystems, each with a
lazy loader and a releaser, both dispatchers called from `ADDR_STATE2_ENTER`.
All sixteen leaves run on a single Boot Camp drive, and `bootcamp` and
`campaign` are clean with them in.

**It was eight variations on one idea, which is the shape that invites
generating six of them from the first two.** What stopped that, each a one
command check that caught something which would have compiled:

- **pairwise diffs, not against one baseline.** A single-baseline pass called
  pairs 4 and 7 distinct; they are 1.000 identical. A baseline finds twins OF
  the baseline and hides twins among the rest.
- **`[esi-4]` per body, presence AND absence.** Four base conventions across
  the pairs -- and pair 3's loader points at its record base, so carrying pair
  1's correction over would have put that table four bytes early. The first
  instance in this project of applying a base fix where none was needed.
- **a no-writer scan**, which turned an apparent defect back into a constant:
  a global with no writer is a constant, where a FIELD with no writer is dead
  code.
- **reading each `je` target.** One bit between `jl` and `jle` separates a
  correct free from one that leaks eight of thirteen.

Three defect-shaped behaviours are reproduced rather than fixed: the decal
loader's `hotY` taken from the width, that eight-of-thirteen leak, and four
different NULL conventions across the frees.

## Next: the vehicle step, and its entry is already done

`ADDR_STEP_TYPE3` is reconstructed -- found after reading 240 instructions of
it, because I grepped four sibling addresses for patches and not that one. The
outstanding work is its five callees, ~4,880 bytes, all reached from our own
code so their counters will be blind.

The leaf is `0x0045C6E0`, vehicle steering, fully read and scoped in the
scratchpad: three arguments (the `add esp, 0x18` says six and is cleaning a
neighbour's), an acceleration limiter whose two arms are a floor and a
ceiling, and an `esi` that stops being the object and becomes a ROW partway
through.

## Above the line: the two 12-byte row pools

Four functions in, 1,436 patches. `RowPoolAInit`/`BInit` (`0x00460800`,
`0x00460AC0`) and `RowPoolAFree`/`BFree` (`0x00460860`, `0x00460B30`).

**Verified for the inits, NOT for the teardowns**, and the difference is worth
keeping straight:

- The first version omitted `RowAlloc` and `ab.sh bootcamp` returned **293,671
  pixels** with an identical log. With the call in it is **22**, the usual
  baseline, and the 1,610-line object-state dump is identical. So the inits
  are compared against the original on a live map load.
- `ab.sh quit` passed **on the broken build too**, because it leaves through
  the TITLE screen and never enters state 2 -- so the pools are never
  initialised and `ADDR_FREE_SEQ_CONTEXTS`, which is the LEVEL teardown, never
  runs. Both teardowns are therefore unverified, and that clean pass is worse
  than no result because it looks like one.

Reaching them needs a drive that leaves a live mission: poke
`ADDR_MENU_REQUEST` during play, which is how `StopAllSounds` was first
executed. No configuration does it yet.

## THE SUB-CRT BOUNDARY IS CLOSED

**Every game function below `0x0045C000` is reconstructed: 1,239 of 1,239.**
Measured, not asserted -- `docs/functions.tsv` lists 1,239 entries below the
line, 1,238 have at least one `patch_replace` inside them, and the one that
does not is `0x0040A6A0`, which is `WndProc` and is REGISTERED into the
`WNDCLASS` rather than detoured. That is the shape `CLAUDE.md` warns about
under "not every reconstruction is a patch", and a count that only looked at
patches would have reported 1,238 and left someone hunting a function that
was finished long ago.

The last one in was `FlowRecvMessage` (`0x004014C0`, 3,040 B), the
flow-control receive path: three protocol messages -- data, nack and pulse
ack -- eight exits, and a cumulative-ack retirement loop written out twice.

**It is the weakest-verified function in the tree and that should be said
plainly.** No DirectPlay session opens on this machine, so every counter in
it reads 0 on every drive; `tools/vectors.py` cannot take it, because it
reads globals and calls into the image; and `AM2_SELFCHECK=1` cannot, because
the comm object is NULL before `install()`. It is verified by reading, and by
the checks that caught four transcription errors on the way in.

## OPEN: the reconstruction side of `mission` is ~12x slower than recorded

Measured today over three runs: our side composes **533, 652 and 625** frames
where `CLAUDE.md` records a band of 6,291-8,300. The original's side reads
25,563 / 26,305 / 26,433 against its recorded 25,932 / 25,917 / 25,738 -- so
**the original is unchanged and ours is what moved.** Two of the three runs
predate this session entirely.

It is not a correctness problem on the evidence available: the game log is
identical at 13 messages, the object state dump is identical, and the widget
tree is identical at 16 nodes, on every one of those runs.

**Two more samples after FireWeapon landed: 471 and 546, against the
original's 7,443 and 7,343.** Same band as the 533/652/625 above, and the
same three artifacts identical both times. Worth recording because
FireWeapon is the largest function in the tree and lands in the middle of
the weapon path, so it was the obvious suspect the moment the gate failed --
and it is not the cause. The band was already this low on commits that
predate it.

Note the ORIGINAL side also reads ~7,400 here rather than the ~26,000 above,
so both halves moved with the machine between those sessions. Read the
RATIO's direction, not either number alone: what stayed constant across all
five runs is that ours is the short side, which is itself the reverse of
what CLAUDE.md documents for this configuration.

**THE TRACE TABLE IS NOT THE CAUSE. TESTED AND DISPROVED.**

| | original | reconstruction |
|---|---:|---:|
| `TRACE=0` | 25,548 | **604** |
| `TRACE=1` | 25,563 / 26,305 / 26,433 | 533 / 652 / 625 |

604 sits inside the spread of the three traced runs, so turning tracing off
changes nothing measurable. The prediction was written down before the run --
"if both come back ~500-700, tracing is NOT the cause and the hypothesis is
wrong and should be deleted, not amended" -- and this is that outcome, so it
is deleted rather than qualified.

The original's side is an unplanned control and a good one: `AM2_NOPATCH`
installs no stubs whatever `TRACE` says, so its 25,548 against 25,563 is the
instrument reporting no change where none is possible. Four samples over
twelve hours, all 25,500-26,500.

**So the reconstruction composes about 600 frames where the original composes
about 25,500, and WHY IS NOT KNOWN.** What has been ruled out is the trace
stubs. What has not been examined at all: the detour jump at every one of
1,432 patched entries, GCC against MSVC 6 on this code, and whether some
single hot reconstruction is disproportionately slow. The last of those is
the one a profile would find in minutes and nobody has taken one.

`AM2_AB_TRACE` exists now for anyone repeating this. Note that with `TRACE=0`
there are no counters at all, since the counters ARE the stubs.

**FOUR MORE SAMPLES, AND TWELVE COMMITS RULED OUT.** 582, 619, 573 and 589,
taken while chasing what looked like a regression from a seam closure. They
sit inside the 533-652 spread above, so nothing has moved. What they add is
the ATTRIBUTION: 573 is the parent commit with the change stashed, and 589 is
`291581c`, which predates this session entirely -- so none of the twelve
commits made since is the cause, and neither is the change that was under
suspicion when the gate first failed.

Both halves ran the same `am2hook.dll` on every one of those, which the hash
guard in `ab.sh` reports rather than leaving to trust; and `uptime` was 3.8,
well below the 18-21 that makes any A/B figure meaningless.

**Read this section, not `CLAUDE.md`, for the band.** CLAUDE.md still says our
side sits at 6,291-8,300 and calls a departure from it the cheapest possible
check -- which was true when written and is now an order of magnitude wrong.
A commit message in this session asserted the opposite, that STATUS.md was the
stale one; it was not, and the correction is recorded here because getting the
direction backwards is exactly how a stale number survives being noticed.

## OPEN: `ab.sh mpoptions` is broken, and it predates this session

It fails its drive outright -- "could not settle the team button on 1575810
(got )" -- and then reports 221,423 pixels against a budget of 300, with the
reconstruction side logging `Couldn't open bitmap file!` twice where the
original logs the .aai checksums.

**Three commits give byte-identical failures**: the current tree, its parent,
and `291581c`, which predates this session entirely. 221423 every time. So it
is deterministic and none of this session's work caused it -- which is the
only thing established. What it IS remains unknown.

Ruled out on the way: no stale `ArmyMen2.exe`, port 31436 free, no
`bind/listen` line in either log. Those are the two cheap ways this
configuration lies and CLAUDE.md names both.

Worth knowing before trusting it: the last recorded clean `mpoptions` run is
in CLAUDE.md's own notes, so the breakage happened at some point nobody has
bisected. It is the only configuration that reaches the multiplayer widget
tree, so every comm reconstruction verified "by reading" has had no drive
behind it for longer than anyone has checked.

## What is next, as a number rather than a direction