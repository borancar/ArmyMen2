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
| win32/widget.h:773 | `unused5B4` | 0x05B4  HUDLOG_OFF_FIELD_5B4 |

## Names that are readings rather than proofs

- `AM2_Object::moveMode` (was `field540`): a unit's walk-state selector; set by the script event
  `EvtSetField540` and by placement, switched on in the trooper step. The values 1..3 are not decoded.
- `AM2_Object::convertedFrom` (was `field5A4`): 1-based army the unit was before it changed owner
  (`prev + 1`), 0 for none; the death handler reports the kill to that army.
- `AM2_Object::turretFree` (was `field548`): 0 slaves the turret to the hull heading.
- `AM2_Flow::roundTrip` (was `field68`): only reader halves it to estimate the peer's clock base; nothing
  writes it, so it is always 0.
- `AM2_SightOut::fromNet` (was `field00`): set when a network update supplies the facing or mode, and
  when a shot is decided; the sight record is only refreshed from the weapon pose while it is 0.
