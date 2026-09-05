# Object types

The registered-object table keys everything on a type byte, and seven of the
eight values are identified. They were NOT guessed: each comes from a function
that names it, or from a per-type loader that puts a named table into the
record it builds.

| type | what it is | what said so |
|---|---|---|
| 1 | item | shares `FreeItem`'s arm with 5, 6 and 7, which names none of them |
| 2 | trooper | `FreeItem`'s arm logs `"DestroyTrooper %x"` |
| 3 | vehicle | that arm is `DestroyVehicle`, and the destroy handler clears a footprint out of `ADDR_VEHICLE_MASK` |
| 4 | weapon | `FreeItem`'s arm names it |
| 5 | missile | `LoadType5` puts `ADDR_MISSILE_ANIMS` -- missile.ani -- into the row it builds |
| 6 | explosion | `StepType6` and the `BLAST_OFF_*` fields |
| 7 | *unread* | the last one |
| 8 | roach | the matching clearer indexes `ADDR_ROACH_MASK` with no kind index |

**Reach for `LoadTypeN` when a type is unidentified.** The per-type SAVEGAME
LOADER is where a type's constants all appear at once -- the box, the row spec,
the anim table and the def record -- which is how 5 and 8 were settled.

**And a shared teardown arm names nothing.** Types 1, 5, 6 and 7 share one
`FreeItem` arm; three of those four are identified from elsewhere and not one
of them came from that arm.

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
