# Struct members still named by offset

Left after the 2026-10-29 naming pass (`git log --grep="Name "`). Each is a member whose role the
code does not show: it is only zeroed or copied, or its one reader does not say what the value means.
Names that came from evidence (uses, or the column headers of the game's own `aai/*.aai` tables) are in
the headers; this list is what is left.

| header:line | struct | member | what the header says |
|---|---|---|---|
| globaltypes.h:136 | AM2_ScenRow | `field12` | +0x12 SCEN_ROW_OFF_FIELD_12, forced to 1 if zero |
| globaltypes.h:139 | AM2_ScenRow | `field68` | +0x68 SCEN_ROW_OFF_FIELD_68 |
| globaltypes.h:144 | AM2_ScenPart | `field0` | +0x00 cleared, never filled |
| globaltypes.h:180 | AM2_FontRec | `field208` | +0x208 |
| map.h:82 | AM2_MapRec | `field11` | +0x11 written 0 |
| maprow.h:205 | AM2_Seq | `field20` | +0x20 |
| msgslot.h:146 | AM2_Flow | `field34` | +0x034 |
| msgslot.h:157 | AM2_Flow | `field60` | +0x060 |
| msgslot.h:158 | AM2_Flow | `field64` | +0x064 |
| msgslot.h:159 | AM2_Flow | `field68` | +0x068 halved into FLOW_OFF_PEER_CLOCK |
| msgslot.h:160 | AM2_Flow | `field6C` | +0x06C |
| msgslot.h:168 | AM2_Flow | `field98` | +0x098 |
| msgslot.h:169 | AM2_Flow | `field9C` | +0x09C |
| msgslot.h:170 | AM2_Flow | `fieldA0` | +0x0A0 |
| msgslot.h:173 | AM2_Flow | `fieldAC` | +0x0AC |
| msgslot.h:655 | AM2_CommSlot | `field5C` | +0x5C COMM_SLOT_OFF_FIELD_5C |
| msgslot.h:666 | AM2_Comm | `field000` | +0x000 |
| msgslot.h:702 | AM2_Comm | `field41C` | +0x41C |
| objtable.h:284 | AM2_SightOut | `field00` | +0x00 cleared when a shot is decided here |
| origstate.h:665 | AM2_OrigState | `unused_662288` | 0x00662288 |
| script.h:356 | AM2_ScriptAction | `unused0c` | +0x0C, never written here |
| script.h:357 | AM2_ScriptAction | `unused10` | +0x10, never written here |
