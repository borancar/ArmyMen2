# Jump tables, dispatch tables and the tables they index

This image dispatches through byte tables constantly, and **reading the arms in
order without decoding the table invents behaviour**. Six distinct failure
modes have been hit here, and every one of them compiles, passes every static
check, and is invisible to an A/B because each indexes SOMETHING correctly.

| failure mode | instance |
|---|---|
| the arms are laid out in a different order from the table | `WeaponClassOf` -- kinds 2,3,4,5 answer 2,3,1,4 |
| several slots share one arm | `SpriteKeyForKind` -- eight slots, six targets |
| an arm ENDS INSIDE another | `UnitKindMatches` kind 3 jumps into kind 4's tail |
| an arm is unreadable from its own body | `PlacementAllowed` -- the scramble is the TABLE's |
| most of the table is impossible | `AddSightBlocker` -- 8 of 16 codes cannot occur |
| the table is a FILTER, not a dispatch | `TrooperFire` -- nineteen indices, two arms |

**The rule that follows from all six: dump the table before writing anything
down about the order, and for a table of any size GENERATE the code from it
rather than transcribing it.** A hand-written mapping of eighty-one codes onto
twenty-four arms put six in the wrong arm, and no amount of re-reading found
it -- what found it was diffing the groupings against the image's own table
programmatically.

And **find the table that gives the numbers NAMES before reading the arms**:
the item captions at `0x00419A30` turn `ObjCodeUnmapped`'s five zero entries
from numbers into AIRS, PARA, RECO, MAG and AERO.

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
