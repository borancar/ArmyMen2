# The combat layer

**No configuration in this project reaches combat**, and that is one
measurement rather than a dozen separate mysteries. On a Boot Camp mission
driven past both dialogs, with the frame ticking and `ObjIsItem` past 84,000,
`ShotStrike`, `ApplyShotDamage`, `DamageObject`, `CreateMissile`,
`FireWeaponAtPoint` and `FireWeaponAtObject` ALL read 0. Sarge stands there;
nothing fires and nothing is hit.

So everything whose only route is "something took damage" is cold for ONE
reason, and the whole layer is checked by enumerating oracles --
`tools/shotcheck.py`, `tools/shotdmgcheck.py`, `tools/damagecheck.py` -- or not
at all. See `docs/oracles.md` for the family they belong to.

The lesson this layer taught that generalises furthest: **a HOT function can
still be undiscriminated.** `ShotStrike` ran 13,582 times on a driven mission
and the suite could not see a mutation to it, because almost all of those calls
find nothing at the point and fall straight through. Ask which BRANCH the calls
take, not how many there are.

**THE SHOT CHAIN IS CHECKED END TO END NOW** -- `shotcheck` over
`ShotStrike`, `shotdmgcheck` over `ApplyShotDamage`, `damagecheck` over
`DamageObject`. All three are unreachable the same way: no drive here reaches
combat, and each one's caller is ours, so every counter in the chain is blind
and reads 0 whatever happens.

`ApplyShotDamage`'s substance is the ARGUMENTS it computes. The amount is
scaled by a switch with four behaviours and the damage KIND differs on
exactly one arm -- the random one answers 1 where every other answers 2,
which no A/B could see because that number never reaches the screen. `rand`
is stubbed to a value each case chooses, which is what makes the modulus
comparable at all; with a real generator the same case answers differently
every run.

**AND IT CONFIRMED A FINDING FROM ANOTHER TOOL, which is what turns a quirk
into a contract.** All 92 cases differed at first, only in `DamageObject`'s
FIFTH argument: 0x00BB0090 against the 0x90 the C computes, with the
shooter's uid showing through above the biased facing. `tools/roachbitecheck.py`
records exactly this at the same argument of the same callee, from a
completely different caller -- so only the low byte is the function's, and it
is `DamageObject`'s contract rather than one caller's accident. Two
independent callers agreeing is better evidence than either alone, which is
the same standard this file applies to two routes to one fact.

**`tools/shotcheck.py` CHECKS THE FUNCTION THIS FILE CALLS COVERED AND
UNCHECKED, and it catches the exact mutation that defeated the A/B.** Making a
non-explosive shot PENETRATE instead of stopping at the first thing it damages
left `combat` clean -- 21 identical messages, frames in step. Here it fails 17
of 107 cases. The corpus is what makes the difference: a list of ONE object
cannot tell stopping from continuing, and almost every live call finds nothing
at the point at all.

**THREE MUTATIONS FAILED NOTHING UNTIL THE CORPUS LEARNED TWO THINGS ABOUT
CONTROL FLOW**, and both are worth stating because neither is about inputs:

- **the terrain test is only reached when the walk does not return.** A
  non-explosive shot leaves the moment it damages anything, so an `attr`
  sweep run with one hittable object never reaches the ground test at all --
  turning its `<` into `<=` failed not one of 55 cases. Sweeping over an
  EMPTY list too takes it to 8.
- **the loop's write to FIELD_44 is observable only on the early ground
  return.** For an explosive shot the `code == 3` arm rewrites that field with
  the same value, so unless `attr < height` sends the function out first, what
  the loop decided is overwritten -- and the two skips, the trooper and the
  flagged item, change nothing anyone can see. They fail 4 and 2 now.

A corpus can be rich in INPUTS and still never reach a branch, because what
gates it is where an earlier arm RETURNED. Ask which exit the case takes
before crediting it with covering anything past that exit.

**`tools/damagecheck.py` IS THE COMBAT LAYER'S ORACLE, for the reason
`aicheck.py` is the AI's.** With no drive reaching combat and all four
counters blind, `DamageObject` is checked there or nowhere. It is the
family's convergence point -- three refusals, a four-way dispatch on the
object's type, and a death tail -- so 95 cases over its arms compare the
TRACE, which is what a void function's output actually is.

Two things the corpus had to be built to reach, neither of which a live drive
would produce:

- the health test is asked TWICE, once before the dispatch and once after.
  A stub that leaves health alone can only ever reach the first, so the four
  damage stubs SUBTRACT and the corpus carries blows that do and do not kill.
- `CommMustBroadcast` is asked in THREE places with two different arguments --
  the attacker's army twice, and the VICTIM's on the last refusal. A stub
  answering from one flag makes those indistinguishable, so it answers per
  army and the corpus varies the two independently. This is the only thing
  that exercises those arms at all: `ADDR_MP_SESSION` is 0 on every drive
  here, so all three broadcast refusals are unreachable in play.

**AND TWO MUTATIONS PASSED UNTIL THE CORPUS GAINED A DIMENSION.** The deepest
tail -- the selected-count test, the leader lookup and the re-select -- is
behind `victim owner == g_defaultOwner`, and every case had the victim on
another army, so the function returned before reaching any of it. Dropping
the count test and inverting the leader test both failed NOTHING. With the
victim's army made a dimension they fail 2 each, which is exactly the number
of cases that reach them. Third instance of the boolcheck trap in this file
and the first where the missing dimension was an IDENTITY rather than a
value.

**MOVEMENT IS FACING-RELATIVE, and not knowing that wastes a drive.** Action
0 walks FORWARD along the unit's own facing; actions 2 and 3 TURN, on a
shared repeat delay. So holding the key bound to "up" does not move north --
measured, it took Sarge from 1782,1084 to 1424,1084 and then to 1093,968,
i.e. west and then north-west, because that is where he was pointing. A drive
that assumes screen axes will conclude the unit is stuck when it is walking
perfectly well in a direction nobody asked about.

**AND THE COMBAT LAYER STILL HAS NO DRIVE, now for measured reasons rather
than assumed ones.** Three probes, all negative, recorded so they are not
repeated:

- Boot Camp holds exactly THREE enemy troopers, at ~1,330 world units from
  Sarge's start and reachable only by a long walk in one direction. Eight
  rounds of the `combat` drive move him about 500.
- MAP 01 is described here as hostile the moment its dialog clears, and that
  does not mean damage arrives quickly: driven into live play and left for 46
  seconds, NOTHING among its 331 objects is below its maximum health -- on
  our build AND under `AM2_NOPATCH=1`, identically.
- and the counters cannot stand in, being blind.

The one thing those runs did establish is a new matching observable: the
campaign in live play gives 331 objects with identical health on both sides.
What is still missing is a target whose health MOVES, and until one is found
the consequence layer is verified by reading.

**AND THE FIGURE BELOW IS HISTORY NOW, which is the stale-band lesson again
one layer in.** `ShotStrike`'s 13,582 was a real measurement when its callers
were still the image's. They are ours, and `tools/blindspots.py` reports
`ShotStrike`, `ApplyShotDamage`, `DamageObject` and `FireWeaponAtPoint` ALL
BLIND -- so a fresh `counts` on a driven combat run reads 0 for every one of
them and that reads exactly like "combat never happened". It was nearly
written up that way here.

So the counts in the paragraph below stand as history and the ARGUMENT they
support is unaffected, but nothing in the tree reproduces them any more. What
is left is the observable this file already prescribes for the class: an
object's HEALTH in `tools/objdump.py --table`. On a driven Boot Camp run
after eight rounds of walking and firing, nothing among the 1,609 objects is
below its maximum -- which is consistent with the note below that NOTHING
DIES on that stretch, and is not by itself evidence either way about whether
a shot was fired. Settling that needs a target whose health moves, and
finding one is the open work.

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
