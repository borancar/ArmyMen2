# Member naming: what a name here does and does not claim

Every struct member has a role name, with two grades:

1. **Named from evidence.** The role is read off the code that uses the member, or off the column
   headers of the game's own definition tables (`aai/weapon.aai`, `vehicle.aai`, `troop.aai`,
   `object.aai`). `speed`, `sightHits`, `zVel`, `fwdSpeed`, `viewAngle`, `perm` and so on.
2. **`unusedXX`**: the member exists in the original record at that offset but the whole
   reconstruction never reads it (it is only zeroed, copied whole with its record, or parsed and
   dropped). The name states that fact, not a guess at a purpose; a reader of the original that
   turned up later would replace it. The offset in the name keeps it greppable against `orig.h`.

## The `unused` members (34)

| header:line | member | note |
|---|---|---|
| globaltypes.h:136 | `unused12` | +0x12 SCEN_ROW_OFF_UNUSED_12, forced to 1 if zero |
| globaltypes.h:139 | `unused68` | +0x68 SCEN_ROW_OFF_UNUSED_68 |
| globaltypes.h:144 | `unused0` | +0x00 cleared, never filled |
| globaltypes.h:180 | `unused208` | +0x208 |
| map.h:82 | `unused11` | +0x11 written 0 |
| maprow.h:205 | `unused20` | +0x20 |
| msgslot.h:146 | `unused34` | +0x034 |
| msgslot.h:157 | `unused60` | +0x060 |
| msgslot.h:158 | `unused64` | +0x064 |
| msgslot.h:160 | `unused6C` | +0x06C |
| msgslot.h:168 | `unused98` | +0x098 |
| msgslot.h:169 | `unused9C` | +0x09C |
| msgslot.h:170 | `unusedA0` | +0x0A0 |
| msgslot.h:173 | `unusedAC` | +0x0AC |
| msgslot.h:655 | `unused5C` | +0x5C COMM_SLOT_OFF_UNUSED_5C |
| msgslot.h:666 | `unused000` | +0x000 |
| msgslot.h:702 | `unused41C` | +0x41C |
| script.h:356 | `unused0c` | +0x0C, never written here |
| script.h:357 | `unused10` | +0x10, never written here |
| win32/movie.h:32 | `unused20` | +0x20 |
| win32/widget.h:373 | `unused74` |  |
| win32/widget.h:447 | `unused20` | +0x20 |
| win32/widget.h:448 | `unused24` | +0x24 |
| win32/widget.h:458 | `unused30` | +0x30 |
| win32/widget.h:459 | `unused34` | +0x34 |
| win32/widget.h:495 | `unused90` | 0x0090 |
| win32/widget.h:522 | `unused6C` | 0x006C |
| win32/widget.h:532 | `unused58` | 0x0058 |
| win32/widget.h:536 | `unused68` | 0x0068 |
| win32/widget.h:629 | `unused4` | 0x0004 |
| win32/widget.h:724 | `unused7C` | 0x007C |
| win32/widget.h:726 | `unused84` | 0x0084 |
| win32/widget.h:736 | `unused60` | 0x0060 |
| win32/widget.h:773 | `unused5B4` | 0x05B4  HUDLOG_OFF_UNUSED_5B4 |

## Why `unused` is a statement about the shipped program, not only about our source

The reconstruction is byte-identical to the shipped image in 1,639 of 1,646 functions, and the
other seven differ in register choice or block layout (`docs/bytematching.md`), not in which
fields they touch. A function that matches byte for byte performs exactly the memory accesses
the source spells, so a member the source never reads is a member the binary never reads. The
members above are written only by the code that zeroes or copies whole records (reset blocks,
the slot shift in `CommRemovePlayer`, the parsers).

`tools/offsetreaders.py OFFSET... [--file SUBSTR]` cross-checks that independently of the
source: it lists the image's own functions that use a `[reg+OFFSET]` operand. Looked at for
every `unused` member, the hits are stack slots (`[esp+0x34]` in `FlowRecvMessage`,
`[esp+0x208]` in `SaveGame`) or other structures sharing the number (the object's `+0x98/+0xA0`,
the player message's `+0x9C/+0xA0/+0xAC` in `ReceivePlayerMsg`, the slot's `+0x60/+0x64`), never
a Flow, comm, font, map-record or widget access. No string in the image labels them either: the
program's own names for flow fields are `gotseq`, `hehas` and `active`, already used
(`nextExpected`, `heHas`, `ready`).

A role name for one of these would therefore be invented, which is worse than saying what is
established: the member exists at that offset and nothing the program does depends on it.

## Names that were readings, and what settled them

- `AM2_Object::aiPose` (was `moveMode`, `field540`): the script command `setaipose {stand|kneel|prone|none}`
  (action 0x2B, `EvtSetAiPose`) writes 1/2/3/0, placement writes 1 on a pillbox occupant, and the trooper
  step switches on it to choose the walk/pose state. Settled by the command's own parser.
- `AM2_Object::kind7Army` (was `convertedFrom`, `field5A4`): army + 1, written only by `Type2ActionC`,
  which puts the unit on soldier kind 7 and is reached from menu arm 42 (the actor's owner) and a script
  action (an army). The idle step reports to that army's leader; 0 = none. Named for the kind it belongs to.
- `AM2_Object::manualAim` (was `turretFree`, `field548`): set whenever the player turns the turret (keys or
  mouse); while 0 the vehicle AI sets the gun aim to the hull heading.

## One name that is still an inference

- `AM2_Flow::roundTrip` (was `field68`): its only reader subtracts half of it from the tick count to
  estimate the peer's clock base, so it is a round-trip estimate; nothing writes it, so it is always 0.
  The function that reads it says as much in a comment, but the program names it nowhere.
