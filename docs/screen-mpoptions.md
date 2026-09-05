# The multiplayer options screen

How `MpPanelConstruct` was brought from a process that DIED when the screen was
requested -- 221,423 differing pixels, which was one side showing a panel and
the other showing whatever was on screen when it fell over -- to `ab.sh
mpoptions` reading `widgets identical (131 nodes)`, `state identical`, `log
identical (35 game messages)` and 151 pixels inside a budget of 300.

Kept because the six defects behind it are six DIFFERENT shapes, each of which
cost a separate investigation, and because the rules CLAUDE.md states about
unbuilt fields, constructor order and controls were all either learnt or
re-learnt here. `CLAUDE.md` keeps the rules; this keeps the evidence.

The short version, which is the part worth carrying:

| field | how it announced itself |
|---|---|
| `ARMY_ROWS` | a crash on opening the panel |
| `GAME_BOX` | an empty script name, so the map lookup searched for "" |
| `CHATBOX` | a crash on the first chat line, one dereference PAST the read |
| `GAME_BAR`, `MAP_BAR` | four misplaced nodes in the widget tree |
| `COLOUR_SEL` | a per-row selection that must start at -1 |

All six were found by ONE grep -- `orig.h`'s `MP_PANEL_OFF_*` against the
constructor -- before any of them was diagnosed. A field our code reads and our
code never writes is the cheapest defect in this project to find and among the
dearest to find by its symptoms.

**`mpoptions` HAS A STANDING FAILURE, and its pixel count is the fingerprint
that identifies it.** It logs "Couldn't open bitmap file!" twice on the
reconstruction side, cannot settle its team button, and reports **221,423**
differing pixels. That exact number appears in commit 2f55eb3 from a previous
session and again on two runs today, so a run reporting it is reproducing the
known defect rather than finding a new one.

**ITS MECHANISM IS A GARBAGE SPRITE NAME, localised now.** A probe in
`LoadBitmap` logging its `name` argument and `__builtin_return_address(0)`,
driven through `ab.sh`'s own mpoptions sequence -- MULTI-PLAYER, TCP/IP,
SELECT, then poking the comm host flag and the menu-request pair -- catches
four calls as the panel opens:

    name=00485E30 caller=PreloadSpriteName+0x7E first="type"
    name=00485E44 caller=PreloadSpriteName+0x7E first="oyMe"
    name=0076FD50 caller=PreloadSpriteName+0x7E first="bad_"
    name=0076FC48 caller=PreloadSpriteName+0x7E first="bad_"

The first two point into the MIDDLE of strings in the image's `.data` pool --
"type" and "oyMe" are the tails of longer literals, which is why the failure
message prints `Unable to load sprite oyMessageSend: uid=%x, ...`: the `%s`
argument is a mid-string pointer and the logger walks it to the next NUL. That
is a table indexed wrongly, not a name built wrongly.

`PreloadSpriteName` passes its `name` straight through to `LoadBitmap`, so the
bad pointer arrives from ITS caller. **A second probe names that caller:
`MultiSpriteConstruct`, at +0x50 and +0xD5** -- two call sites, because
`PanelConstruct` is inlined into it, so BOTH `b0` and `b1` arrive wrong.

**AND THAT CONNECTS TO A SYMPTOM THIS FILE ALREADY RECORDED WITHOUT A CAUSE.**
It says `MultiSpritePaint` runs 9,081 times on the multiplayer path and that
"the sprite is null on every call and the function never draws at all" --
found by mutating it and watching nothing change. This is why: the constructor
hands `LoadBitmap` a name that cannot open, the load fails, and the slot keeps
the null. One probe turns a recorded oddity into a diagnosis.

**THE ARITY IS NOT THE FAULT, checked the way this file prescribes.** The
original at 0x00456BC0 ends `ret 0x1C` -- 28 bytes, seven dwords -- which is
exactly `b0, b1, flag` plus the four of a `RECT` passed by value, matching our
signature. (Its entry also shows a `ret 4`, which belongs to the next function
inside a merged `functions.tsv` row, not to this one.) So the caller really is
supplying mid-string pointers rather than our frame reading the wrong slots.

**THE POINTERS ARE LOG FORMAT STRINGS.** `0x00485E30` is `"type %d\n"` and
`0x00485E44` is `"oyMessageSend: uid=%x, pos=("`, where the names the panel
should pass are `0x0048701C` `"03_028_00_green.bmp"` and `0x00487030`
`"03_028_01_green.bmp"` -- or the red pair at `0x00487178`/`0x0048718C`.

**EVERY OTHER LINK IN THE CHAIN CHECKS OUT**, which is what makes this narrow.
All fourteen original callers of `MultiSpriteConstruct` push one of those two
correct pairs, in the order our signature expects -- the `_00_` name pushed
last, so it lands as `b0`. All eight of OUR call sites pass a literal or a
macro, and every `AM2_BMP_*` macro resolves to a real `.bmp` name; the only
two that do not are record SIZES rather than addresses.

**AND THE CALLER RESOLUTION WAS CHECKED RATHER THAN ASSUMED**, which nearly
went wrong. The DLL is relocated, and the first resolution used a base taken
from an EARLIER run's log -- which would have named the wrong function had it
moved. The run's own `patch:` lines settle it with no assumption:
`patch: MultiSpriteConstruct 00456bc0 -> 770623F0`, and the probe's callers
were that address plus 0x50 and plus 0xD5. **Read the base out of the run you
are diagnosing, not out of a previous one.**

**FOUND AND FIXED: `MpPanelConstruct` HAD TWO RAW HEX LITERALS POINTING AT LOG
FORMAT STRINGS.** A probe on `MultiSpriteConstruct`'s own caller named
`MpPanelConstruct + 0x638`, and the source there read

    MultiSpriteConstruct(child,
                         (const char *)AM2_IMAGE(0x00485E30u),
                         (const char *)AM2_IMAGE(0x00485E44u),

where the original pushes 0x487178 and 0x48718C -- `AM2_BMP_RED0` and
`AM2_BMP_RED1`, the red ready lamp. The block's own comment says "the two ready
lamps, green and red"; the green one above it uses the macros correctly and
this one was transcribed as bare addresses that landed four kilobytes short,
inside the log format pool.

With the macros in place the two "Couldn't open bitmap file!" lines are GONE --
2 to 0 -- so the lamp loads. That also settles this file's older note that
`MultiSpritePaint`'s sprite "is null on every call": one of the two lamps was
never loading.

**THE SECOND DEFECT IS LOCALISED: THE MAP FILE IS NOT FOUND.**
`RefreshMapSelection` is what computes the three handshake checksums, and it
returns BEFORE them when `FileExists("<map>.amm")` fails -- zeroing
`g_mapChecksum`, clearing the comm record's map-ok flag, and calling
`ShowBadMapPreview`. That accounts for all three symptoms at once: no
`Checksum of ...` lines, the `"bad_"` names the first probe caught going to
`LoadBitmap`, and a quarter of the frame differing because the preview pane
shows the bad-map bitmap instead of the map.

The path is built by `SetGameDir(ADDR_MAP_FOLDER)` then `sprintf("%s.amm",
ADDR_MAP_NAME)`, and comparing those two globals at the moment the panel opens
settles it outright:

| | `ADDR_MAP_NAME` | `ADDR_MAP_FOLDER` |
|---|---|---|
| original | `alpine3_mp` | `data\mpalpine` |
| ours | **empty** | **empty** |

So our `sprintf` produces `.amm`, `FileExists` fails, and the function takes
its bad-map exit. Both globals are written in exactly one place --
`SelectLevel` at `map.cpp:223`, copying `LEVEL_OFF_MAP_NAME` and
`LEVEL_OFF_FOLDER` out of a level record -- and **our `MpPanelConstruct` never
calls it.**

The original does, at 0x00431242, from a default-map step our reconstruction
omits entirely:

    edx = [esp+0x1c]              ; the map-list object
    ecx = [esp+0x20] << 6         ; index * 0x40
    eax = [edx + 0xC8] + ecx      ; the name at that row
    FindLevelByName(eax)
    if (eax) SelectLevel(eax)

Ours calls `ReadMpMapList()` and then builds the list box with NO ROWS AT ALL
-- `ListBoxConstruct(child, 0x16, 0xC8, 0xFA, 0x5A, (void *)0, 0, 0, 1)` --
so the omission is larger than the selection. The original's loop at
0x004311A1 is what fills it:

    for (i = 0; i < maps[0xC4]; i++) {
        lvl = FindLevelByName(maps[0xC8] + i * 0x40);
        if (!lvl) continue;
        ListAdd(list, lvl + 0x44, lvl);      /* the row's caption and data */
        if (strcmp(lvl + 4, ADDR_MAP_NAME) == 0)
            sel = i;                          /* remember the current map */
    }
    lvl = FindLevelByName(maps[0xC8] + sel * 0x40);
    if (lvl) SelectLevel(lvl);

`sel` starts at 0, which is why the original lands on `alpine3_mp` with an
empty `ADDR_MAP_NAME`: nothing matches, so the first row wins.

So the multiplayer map list is EMPTY in our build and no map is ever selected.

**THE BLOCK IS WRITTEN, AND IT IS INERT BECAUSE A PREREQUISITE IS WRONG.**
`MpPanelConstruct` now builds the rows record, walks the map list, adds each
level it finds and selects one -- transcribed from 0x004311A1, with
`MPMAPS_OFF_COUNT`, `MPMAPS_OFF_NAMES` and `AM2_MPMAP_STRIDE` added to
`orig.h` for the three constants the loop reads.

It changes nothing yet, and a probe says exactly why: `ScriptListFind` is
handed the buffer at `ADDR_MP_SCRIPT_NAME`, and in our build that buffer holds
**"death.txt"** at the moment the panel opens, so the lookup answers null and
the guarded loop does nothing. The name is written in two places -- once at
startup in `winmain.cpp` and once in `misc.cpp:1169`, which copies the FIRST
NAME-TABLE RECORD into it and the record's `NAMEREC_OFF_MAPS` into
`ADDR_MAP_NAME`. Our `ADDR_MAP_NAME` comes out empty from that same copy, so
both globals are being filled from a record that is not the one the original
reads.

**AND `ApplyGameSettings` IS MISSING THE ELSE BRANCH.** The original at
0x0042F218 tests `ADDR_NAME_TABLE_COUNT`, copies into `0x511C08` and
`0x511A88` when it is non-zero, and otherwise CLEARS both --
`mov byte [0x511c08], bl` and `mov byte [0x511a88], bl` at 0x0042F273 and
0x0042F279. Ours has only the copying arm, so on an empty table it leaves
whatever was already in them, which is how `ADDR_MP_SCRIPT_NAME` comes to hold
"death.txt" while `ADDR_MAP_NAME` is empty -- two globals that the original
would have made consistent.

The branch is restored, and the run says it is NOT SUFFICIENT: `mpoptions`
still reports its unchanged 221,423 pixels. So the missing else was a real
transcription defect and not this screen's cause -- worth having on its own
terms, and worth saying plainly that fixing it changed nothing visible.

It is safe, which is the part that needed measuring rather than arguing:
clearing `ADDR_MAP_NAME` could have reached single player, and `ab.sh bootcamp
campaign` is clean with it in -- 1,610 state lines and 13 messages identical,
35 widget nodes identical.

**AND IT IS NOT ZERO. `mpoptions` FAULTS, and the screen was never a drawing
difference at all.** Every reading above assumed the panel came up and looked
wrong. It does not come up: our build EXITS when the multiplayer options
screen is requested, and 221,423 pixels is one side showing a panel and the
other showing whatever was on screen when the process died.

Measured, with the two sides driven identically by the same pokes:

| | original | ours |
|---|---|---|
| alive after the request | yes | **no** |
| `ADDR_NAME_TABLE_COUNT` | 6 | 6 |
| `ADDR_LEVEL_TABLE_COUNT` | 10 | 10 |
| handshake checksums logged | six | none |

So both tables parse correctly -- 6 `RULES` and 10 `MAP` lines, which is
exactly what `mpmaps.txt` declares -- and the name-table theory this paragraph
spent three commits on was answering a question that was not being asked.

**Two false trails, both killed by measurement rather than argument.** The
game logs `Lobby start: about to call ReadMpMapsFile` and `Releasing Comm
Connection` just before dying, which reads as the lobby arm `State1Enter`
already has a scar for -- and the ORIGINAL logs both lines too, at startup, on
its way to the quiet `DPERR_NOTLOBBIED` exit. And the map-list block added to
`MpPanelConstruct` was the obvious suspect, being the newest code on the
screen; disabling it behind a switch leaves the exit exactly where it was.
**Diff the logs before believing a message is a symptom** -- a line that
appears on both sides is startup, however incriminating it reads.

**The fault is `MpPanelUpdate` +0xa2, and resolving it needed the DLL base
from the RUN'S OWN patch lines.** Wine reports `c0000005` at a runtime
address inside `am2hook.dll`; two anchors from that run's log give the same
delta, which is the check that the base is right rather than remembered --
this file already records a caller mis-resolved from an earlier run's log.
The faulting instruction is `mov 0x58(%eax),%eax` with `eax = rows[i]`.

**`MP_PANEL_OFF_ARMY_ROWS` (+0x258) IS READ AND NEVER WRITTEN.** One grep says
so: `MpPanelUpdate` walks it every frame and nothing in the tree stores to it,
so the four army-points rows are whatever the allocator left. The original
does the identical pair of dereferences, so it is not the update that is wrong
-- it is that our `MpPanelConstruct` never builds those four rows.

That is the defect, and it is deliberately NOT fixed in the same commit as its
diagnosis: the original fills the array through a walking pointer rather than
a `+0x258` literal, so a scan for the offset finds nothing and the block has
to be read out of the constructor. Writing it from the shape of the update
would be inventing a layout, which is what this file's whole offset section
exists to stop.

**THE BLOCK IS NOW DECODED AND THE WALKING POINTER IS WHAT MAKES IT LEGIBLE.**
`lea ebx, [ebp + 0x230]` at `0x00430602` walks all four arrays in ONE loop:
`[ebx-0x10]` is NAMES, `[ebx]` COLOURS, `[ebx+0x10]` TEAMS and `[ebx+0x28]`
ARMY_ROWS. We build three quarters of that loop and stop -- so this is one
missing quarter of a loop we already have, not a missing function.

The row is a SPIN control, which its callees say outright: `operator new(0x84)`,
`ADDR_MP_SPIN_CTOR`, `ADDR_WIDGET_ADD_CHILD`, and `ADDR_MP_COMMIT_POINTS` as
the handler. Every one is reconstructed, so the block needs no new seam. The
fifteen arguments line up exactly with the SCORE spinner this constructor
already builds forty lines further on -- same shape, different numbers:

    MpSpinConstruct(child, 0xF0, <top>, 0x4C, <height>,
                    ((const int32_t *)ADDR_ARMY_POINTS)[i],
                    0, 0x1388, 0x64, w, MpCommitPoints,
                    ADDR_VIEW_RECT_COLOUR, ADDR_COLOUR_BELOW_BG,
                    ADDR_BACKGROUND_COLOUR, i)

The value confirms the whole reading: it is `[edx]` walking `ADDR_ARMY_POINTS`,
which is the SAME array `MpPanelUpdate` prints into each row -- constructor
seeds it, update displays it. `0`/`0x1388`/`0x64` are the spinner's lo, hi and
step, and the last parameter is `row`, which our own signature names.

**A RECT PASSED BY VALUE IS BUILT BY REUSING RectSet'S OWN ARGUMENTS, and the
`add esp, 4` is the tell.** `RectSet` takes five arguments and only FOUR BYTES
are popped after it -- the buffer pointer -- because the compiler then
overwrites the four remaining pushed dwords in place with the rect's contents
and lets them stand as the by-value RECT. Read as ordinary cleanup that
`add esp, 4` says the arity is one; it is not, and every displacement below it
would shift.

Both open constants were then READ rather than assumed, and one of them
punishes the guess. `top` is `-0x3F - this + esi`, cancelling to 37 + i*32 --
and the same slot pattern gives the names 40 and the colours 39, which is what
makes the arithmetic checkable rather than merely plausible. **`height` is
`0x17 + (i == 3)`**: `cmp edi,3; sete al` at 0x0043078F feeds the rect's
fourth field, so the LAST row is one pixel taller than the other three. A
uniform 0x18 was the obvious guess and is wrong for three rows out of four.

**THE PANEL OPENS NOW, AND FIXING IT NEEDED A SECOND DEFECT IN A FUNCTION THAT
WAS ALREADY "DONE".** Writing the block alone moved the fault rather than
removing it -- from `MpPanelUpdate` to `MpPanelConstruct`, a write to
`0x00000050`, which is `flag50` through a null. The cause is that
`MpSpinConstruct` builds its three children and **never stores them**: the
original writes each into `SPIN_OFF_EDIT`, `SPIN_OFF_UP` and `SPIN_OFF_DOWN`
BEFORE the `WidgetAddChild` that follows it, and then sets the child's own
`ARROW_OFF_FLAG5C` through the field it just wrote.

That is worth more than the crash it caused. `MpCommitPoints` and
`MpCommitScore` both READ `SPIN_OFF_EDIT`, so those handlers have been reading
whatever the allocator left for as long as the function has existed. It passed
every check because committing a spinner needs someone to type in one, which
no drive does, and the score spinner exists only when hosting. **A field the
constructor forgets is invisible until a second consumer wants it** -- the same
shape as the field-pointer rule, one level along.

With both in, the process survives the request and the panel builds: **44
nodes against the original's 44**, most fields identical, where before the
drive got a dead process and no tree at all. `controls` and `campaign` are
clean with it, so the shared spin constructor did not regress the menus.

**A WIDGET ADDED TO THE WRONG PARENT IS OFFSET BY EXACTLY ITS PARENT'S
POSITION, and that is the signature to look for.** Our edit came out at screen
`480,79` against the original's `240,42` -- which is 240+240 and 42+37, the
spin's own origin counted twice, because the three children had been added to
the SPIN when the original adds them to the PARENT. It is not a placement that
is a little wrong; the arithmetic names the culprit outright. Two further
observations agreed before the change was made: the original's arrows and edit
appear at DEPTH 1 in `ctl widgets`, and all three of its `AddChild` calls load
`this` from the slot that the height's own slot fixes as argument nine.

With that and the down arrow's `y + height - 9` -- `lea ecx,[edx+eax-9]` at
0x00456431, where `y + 9` had the two arrows touching -- **every rectangle on
the panel now matches the original exactly.**

**AND THE JOINER-DISABLE ARM IS LEFT OUT ON PURPOSE, which is a measurement.**
Transcribed as it reads -- clear flag50 and set `disabled` on names[i] and the
spin's three children when the comm object's +0x3D8 is zero -- it produced
**22 disabled nodes against the original's 2**, on a run where BOTH sides read
that field as 0 through the control socket with the same comm pointer. So the
arm as written is not what the original executes, and `COMM_OFF_IS_HOST` being
0 at panel-open on both sides says the condition is not simply "are we the
host" the way the name reads. The original also logs six handshake checksums
here where we log none, so something upstream differs and this arm is
downstream of it. **A wrong arm that LOOKS transcribed is worse than an absent
one that is documented**, and the count is what settles which you have.

Ten disabled nodes remain against two, and the other eight are `MpPanelUpdate`
applying the same test per row -- pre-existing code, the same open question,
not a second bug.

**THAT FINDING WAS WRONG AND THE CONTROL WAS UNMATCHED -- SECOND TIME THIS
SESSION, SAME CAUSE.** It was recorded here that `ADDR_MP_SCRIPT_NAME` reads
`death\0txt` on the original and `\0eath.txt` on ours, and therefore that
`ApplyGameSettings` takes opposite arms on the two sides. Re-run with the SAME
`Options.cfg` copied in before each half, both sides read `\0eath.txt` and
both read a name count of 6. The two halves are byte-identical; the earlier
pair came from manual runs whose config files differed, because a previous
run of the panel had persisted a rules name into one of them.

`LoadOptions()` runs immediately before `ApplyGameSettings`, so the config IS
an input to the very buffer being compared -- and `tools/ab.sh` already knows
this, which is the part that stings. It copies `Options.cfg` per side and
leaves `Options.cfg.mpoptions.orig` and `.recon` in its artifacts; those files
were sitting in the directory listing I read and I did not connect them.

**A CONTROL IS NOT MATCHED BECAUSE THE COMMAND LINE MATCHES.** The first
instance this session was a `-dbg` present on one half only; this one is a
file on disk that one half had written earlier. Both times the reasoning built
on top was detailed, internally consistent and wrong. When two runs disagree
about a GLOBAL, ask what on disk feeds it before asking what code differs --
and prefer `ab.sh`, which matches the inputs, over a hand-rolled pair of runs.

What survives the correction, because it was measured on our side alone: a
probe in `ApplyGameSettings` prints `count=0` and the "Lobby start" line
follows it, so the name table really is empty when that function runs and is
populated afterwards by `CommLobbyStart`'s `ReadMpMapList`. That is true of
the reconstruction and, since both sides take the same arm, evidently of the
original too. So the else branch is faithful, fires correctly, and is not the
mpoptions difference.

**AND THE SAME CONCLUSION THEN CAME BACK, MEASURED PROPERLY.** Matched on
`Options.cfg` and sampled on BOTH sides of the poke rather than once:

| | before the poke | after |
|---|---|---|
| original | `\0eath.txt` | **`death\0txt`** |
| ours | `\0eath.txt` | `\0eath.txt` |

Identical before, divergent after -- so the original WRITES the script name
while the panel opens and we do not. The retracted claim was directionally
right and measured wrong; sampling both ends of the event is what separates
"these differ" from "this one changed".

The rest of the chain is measured with the same control: the original's
`ADDR_MAP_NAME` reads `alpine3_mp` and `ADDR_MAP_FOLDER` `data\mpalpine`,
ours are empty and all-zero. `SelectLevel` is their only writer, so ours never
ran, `RefreshMapSelection` built `".amm"`, `FileExists` refused it and the
panel took its bad-map exit -- no checksums, bad-map preview, a quarter of the
frame.

**THE MISSING BLOCK IS THE TYPE LIST, AND ITS LAST ACT IS THE WHOLE PROBLEM.**
The original walks the name table at 0x00430DD9 -- stride **0xCC**, the record
name at +0, the display name at +0x80 -- adding each rules record to the type
box and remembering which one an inlined `strcmp` matches against
`ADDR_MP_SCRIPT_NAME`. Then, whenever the count is non-zero, it copies
`name_table[sel]`'s name INTO that global with an inlined `strcpy` at
0x00430E96, `sel` defaulting to 0.

So on a fresh panel nothing matches, `sel` stays 0, and record 0 -- `death` --
becomes the script name. That is precisely the `death\0txt` observed, and it
is what makes the map-list block below it work: `ScriptListFind` is handed a
name that exists. Ours never writes the global, so its lookup is handed an
empty string and every consequence follows from that one omission.

**A GLOBAL WITH NO WRITER IN OUR TREE, READ BY OUR OWN CODE, IS THE SHAPE TO
LOOK FOR** -- the same class as `MP_PANEL_OFF_ARMY_ROWS` two findings up,
which was also read every frame and written nowhere. Two instances on one
screen says the panel was transcribed as a series of widgets with the
bookkeeping between them left out.

**AND THE THIRD INSTANCE IS A WHOLE WIDGET: `MP_PANEL_OFF_GAME_BOX` IS NEVER
BUILT.** The field is defined in `orig.h` at 0x208 and read by `OnMpGameType`
and its neighbour, and `MpPanelConstruct` never writes it. The original builds
it at 0x00430F26 -- `RectSet(0x152, 0x35, 0x97, 0x42)`, list-box constructor,
row callback `0x00431A30` (`OnMpGameType`), stored to `[ebp+0x208]`, then
added as a child. That is the `338,53,489,119` node the original's tree has
and ours does not, which is also why the two dumps stop lining up past the
list boxes.

So the type-list omission and the missing script-name write are ONE omission,
not two: the name-table walk, the `strcpy` that follows it, the
`FillListFromRules` after that, and the box they all feed are a single block
of the original that is absent from ours.

**A DEFINED-BUT-UNBUILT FIELD IS CHEAPER TO FIND THAN ITS SYMPTOMS.** Three
fields on this one screen were read by our code and written by none of it, and
each cost a separate investigation -- a crash, a bad-map exit, a tree that
stops matching. Grepping `orig.h`'s `MP_PANEL_OFF_*` against the constructor
finds all three in one command -- and run for real it reports SIX:
`CHATBOX`, `COLOUR_SEL`, `GAME_BAR`, `GAME_BOX`, `MAP_BAR` and `MAP_BOX`.
`PREVIEW` is built, so the test is discriminating rather than merely
pessimistic.

**THE GAME BOX IS WRITTEN NOW, AND THE SCRIPT NAME MATCHES THE ORIGINAL BYTE
FOR BYTE** -- `death\0txt` on both sides after the panel opens, where ours was
`\0eath.txt` before. So the name-table walk and the `strcpy` that follows it
are right.

**ITS FIRST PLACEMENT CRASHED, AND THE ORDER IS THE FINDING.**
`FillListFromRules` reaches `panel + MP_PANEL_OFF_TYPE_BOX` and reads that
widget's rows, so calling it before the 0x204 box exists reads a garbage
pointer -- a page fault at `FillListFromRules+0x5c`. The original stores its
six list fields in a fixed order, and the addresses say it outright: +0x204 at
0x0043094E, +0x208 at 0x00430F3A, +0x20C at 0x00430FDD, +0x210 at 0x004312C3,
+0x214 at 0x0043136B, +0x218 at 0x00431481. **Scanning for the stores to a
record's fields gives the constructor's order in one command**, which is worth
having before inserting anything into a constructor this long.

**SPLIT, AND THE CHAIN COMPLETES.** Our TYPE_BOX block was doing two jobs --
building the 0x204 box AND filling it with MAP rows -- where the original
keeps them apart: 0x204 holds the rules file's own lines and 0x210 the maps.
With the three boxes in the original's order, which its field stores give
outright (0x204, 0x208, 0x210), every global on the screen now matches:

| | original | ours, before | ours, after |
|---|---|---|---|
| `ADDR_MP_SCRIPT_NAME` | `death` | empty | `death` |
| `ADDR_MAP_NAME` | `alpine3_mp` | empty | `alpine3_mp` |
| `ADDR_MAP_FOLDER` | `data\mpalpine` | all zero | `data\mpalpine` |
| the three checksums | `df072909717e62202d690575` | all zero | `df072909717e62202d690575` |

**The checksum triple is the one that counts**, because `ab.sh` diffs it as a
`state` artifact with no budget at all -- three values that never reach the
screen, compared exactly, and they agree. That is the strongest evidence this
screen has ever produced, and it went from all-zero to identical.

The ORDER is the whole of it and it is not a detail: the game box writes
`ADDR_MP_SCRIPT_NAME` and the map list looks up BY that name, so running them
the other way round searches for an empty string, finds nothing, and never
calls `SelectLevel`. Take a long constructor's order from its field stores
rather than from reading it top to bottom.

**AND TWO PIXELS BROKE THE DRIVE RATHER THAN THE PICTURE.** The team button
sat at y=37 where the original has 39 -- the colour swatch and the team button
are two halves of ONE row, `r=134,39,152,59` beside `r=191,39,209,59`, and our
colours were already right. `ab.sh` settles that button by grepping its
RECTANGLE out of `ctl widgets`, so a row two pixels off matched nothing, the
settle loop tapped sixteen times and gave up, and every step after it ran
against a panel in the wrong state. Fixed: the warning is gone and the
first-dump diff falls from 15 lines to 9.

That is worth recording as a class. A pixel budget would never have found it;
the widget tree did, and it did so by breaking the HARNESS rather than the
image. **When a drive reports it could not settle, suspect the coordinate it
is matching on before suspecting the button.**

**AND THE NEXT ONE IS THE CHAT BOX, WHICH THE SCOPED CHECK HAD ALREADY
NAMED.** Replaying `ab.sh`'s drive step by step with a `ping` after each, the
socket answers through the panel, the toggle, both team taps, the two swatch
clicks, the chat click and the typing -- and dies on RETURN, the chat SEND.

The arithmetic identifies it to the byte. The fault is `MenuMessage+0x4a`
reading `0x000002FC`; `MenuMessage` follows
`*(screen + MP_PANEL_OFF_CHATBOX)` and then that pointer's
`LIST_OFF_ARROWBAR`, and 0x280 + 0x7C is 0x2FC exactly. `MP_PANEL_OFF_CHATBOX`
is one of the six fields `MpPanelConstruct` never writes, so it holds whatever
the allocator left -- 0x280 -- and the first chat line follows it.

**A never-written field does not fault where it is READ, it faults one
dereference LATER**, which is why this looked like a click-path bug for two
rounds of measurement. The read of `screen + 0x21C` is perfectly valid memory
-- the panel is 0x278 bytes and the field is inside it -- and only the pointer
it yields is nonsense. Check the offsets against the FAULT ADDRESS: a small
number like 0x2FC is a garbage base plus a real field, and solving for it
names both.

That is three of the six unbuilt fields now accounted for by a symptom
apiece -- `ARMY_ROWS` by a crash on panel open, `GAME_BOX` by the empty script
name, `CHATBOX` by this -- and the check that listed all six cost one command.

**THE CHAT BOX WAS BUILT ALL ALONG AND ITS POINTER WAS DROPPED.** That block
constructed the text list with the right rectangle and arguments and then
never stored it, so the field kept the allocator's leavings. Fixing the store
moved the fault ONE dereference further, to `ArrowBarFollowEnd+0x2` reading
NULL+0x58: the list's scrollbar was neither built nor back-linked, and
`MenuMessage` hands `LIST_OFF_ARROWBAR` straight to it. Two faults from one
omitted tail.

**A WIDGET THAT IS CONSTRUCTED BUT NOT STORED IS INVISIBLE TO EVERY CHECK WE
HAVE.** It draws, it is in the child list, it appears in `ctl widgets` -- and
the field that names it is garbage. Only a consumer that follows the field
finds out, which is why this survived until a chat line was typed.

**`mpoptions` is now essentially clean, and the numbers are the finding:**

| | before | after |
|---|---|---|
| pixels | 221,423 | **308**, against a budget of 300 |
| log | DIFFERS, 21 against 35 | **identical, 35 messages** |
| `state` | one line, all zeros | **identical, five lines** |
| widgets | 43 lines against 128 | 131 against 131, 36 differing |

The `state` artifact is the one to read: five exact dumps with no budget --
both checksum rounds and the chat buffer holding `Zulu` -- and they agree
byte for byte.

**IT IS CLEAN NOW: `widgets identical (131 nodes)`, `state identical`, `log
identical (35 game messages)`, 151 pixels inside a budget of 300, and 0 on
both intermediate frames.** The last two steps were ORDER and two more
scrollbars.

**A CHILD LIST IS ORDERED BY WHEN EACH CHILD WAS ADDED, so a block in the
right place with the wrong neighbours is still wrong.** The game and map
boxes were built correctly and built too EARLY -- before the chat list rather
than after it -- and that moved four nodes and renumbered every sprite index
below them. The original's field stores give the order outright and disagree
with reading the function top to bottom: 0x204 at 0x0043094E, **0x21C at
0x00430CA9**, 0x208 at 0x00430F3A, 0x210 at 0x004312C3. The chat box is built
BEFORE the game box, which no amount of looking at the screen would suggest.

Then `GAME_BAR` and `MAP_BAR`, the last two of the six unbuilt fields, each an
`ArrowBarConstruct` immediately after its box with the two back-links every
other bar here already has. With those the tree matches node for node.

So all six unbuilt fields are now BUILT, and the tally is worth keeping:
`ARMY_ROWS` a crash on open, `GAME_BOX` an empty script name, `CHATBOX` a
crash on the first chat line, `GAME_BAR` and `MAP_BAR` four misplaced nodes,
and `COLOUR_SEL` a per-row selection that must start at -1. **One grep found
them all before any of it was diagnosed**, and re-running it now reports
nothing.

**`COLOUR_SEL` was written up as "apparently unread" and that was wrong.**
`OnMpColour` reads `sel[row]` and takes -1 as "not chosen yet"; left as the
allocator's leavings the test fails, the next colour is computed from garbage
and the army handed to `CommArmyOfSlot` is arbitrary. The original writes it
inside the row loop -- `mov dword ptr [ebx + 0x38], 0xffffffff` at 0x0043062A,
`ebx` walking four bytes a row from `this + 0x230`, so it lands on
0x268 + i*4.

The mistake is instructive about the check itself: it asks who WRITES a field
and says nothing about who reads it, so "unbuilt" was read as "unused" for the
one entry that had no other symptom. **A field with no writer is a finding; a
field with no reader is a separate question the same grep cannot answer.**

The confirmation that costs nothing is two SPELLINGS rather than one: the
macro has a single use in the whole tree and no `0x258` literal reaches the
panel. That is the same "grep the address as well as the name" rule this file
already states for naming, used to establish an absence.

Keeping the block is deliberate: it is faithful to the image, it is guarded so
a null lookup does nothing, and `ab.sh multi` is clean with it in -- 9 widget
nodes identical, 7 messages, 0 pixels. It will start working the day the name
is right, and leaving it out would only hide that.

**EVERYTHING THE MISSING BLOCK NEEDS IS NOW IDENTIFIED**, so what is left is
transcription rather than investigation:

| piece | what it is |
|---|---|
| the maps object | `ScriptListFind(ADDR_MP_SCRIPT_NAME)` -- `0x0043E900` on the key at `0x00511C08` |
| its count | `*(int32_t *)(maps + 0xC4)`, and the loop is skipped when it is <= 0 |
| its names | `*(char **)(maps + 0xC8)`, stride **0x40** |
| each row | `FindLevelByName(names + i * 0x40)`, skipped when it answers null |
| the add | `ListAdd(list, lvl + LEVEL_OFF_NAME, lvl)` -- THISCALL, list in `ecx`, caption `0x44`, data the record |
| the match | `strcmp(lvl + LEVEL_OFF_MAP_NAME, ADDR_MAP_NAME) == 0` sets `sel = i` |
| the tail | `lvl = FindLevelByName(names + sel * 0x40); if (lvl) SelectLevel(lvl);` |

The one thing still to establish is where the ROWS record comes from: the
original holds it in `[esp+0x14]` and hands it to `ListBoxConstruct`, where
ours passes `(void *)0`. `DlgSaveListConstruct` shows the idiom -- allocate
`AM2_ROWS_SIZE`, `RecordCtor(rows, 1)`, `ListAdd` into it, then pass it in --
so that is a pattern to follow rather than a puzzle.

Guessing a line into a 4,497-byte panel is how the FIRST bug on this screen
got there, which is why this is specified rather than written. `OpenMpHost` and `OpenMpJoin`
are both faithful (host sets `g_mpSession` 1 and calls the refresh, join sets
2 and does not, exactly as the image does), so the divergence is in what the
map name or folder holds, not in who calls what.

**THE CONFIGURATION STILL FAILS, for a different reason, and the pixel count is
how you can tell.** It is still exactly 221,423, unchanged by the fix, because a
17x16 lamp is not what makes a quarter of the frame differ. Our side's log still
stops at "Releasing Comm Connection" where the original goes on to the data
checksums, so the panel is still not reaching the same point. Two defects on one
screen, and the fingerprint that identified the first is what proves the second
is not it.

**THE GREP WAS RUN AND THE BUG IS ISOLATED.** Fourteen raw addresses remain
inside `AM2_IMAGE()` in `src/game`, and every one resolves to a real `.bmp`
name -- failure, hq, colour, start, ready, options and cancel. The one that
looked wrong is not: the cancel button is built
`ButtonConstruct(child, 0, CAN1, CAN2, ...)` with a NULL normal sprite, and
the original at 0x00431684 pushes `0` in exactly that slot, so two names
rather than three is what the game does.

So `MpPanelConstruct`'s two literals were the only instance, which is worth
knowing before anyone generalises the fix. They remain raw where macros exist
beside them -- a style inconsistency rather than a defect, left alone rather
than churned.

**A RAW ADDRESS WHERE THE FILE HAS A MACRO IS WORTH GREPPING FOR.** Both
literals sat two lines from correct `AM2_BMP_*` uses, and nothing checks that a
`0x0048xxxx` handed to a string parameter is a string. `checkoffsets` counts
offset macros and `checkpatches` counts `ADDR_` aliases; a bare address inside
an expression is invisible to both.


Worth knowing before spending a control run on it: a deterministic pixel count
that matches a recorded one IS the control. It cost one re-run and one search
of the git log to establish that today's failure predates the session's
changes, where reverting `src/` and rebuilding would have cost twenty minutes.
**Search the history for the number before bisecting for the cause.**
