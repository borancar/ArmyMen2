# Status

A dated snapshot of where the reconstruction is, kept so a session does not
have to re-derive it. **`CLAUDE.md` and `docs/` are authoritative**; this file
is a summary and can be stale between updates. Every number below carries the
command that produces it, so it can be re-measured rather than believed.

Last updated: **2026-09-05**, native build landing (see the first section).

## THE GAME RUNS AS A LINUX EXECUTABLE

`make native` builds `build/armymen2`: the same reconstruction as the
standalone, linked as an i386 ELF against `src/platform/` -- the Win32 and
DirectX surface `src/game/win32/` calls, implemented over SDL3 -- instead of
against Wine. One executable, no runner. SDL is the outermost layer; every
Win32, GDI, DirectDraw, DirectInput, kernel and CRT call the game makes is
emulated to it below the window.

    make native
    AM2_GAMEDIR=".wine/drive_c/GOG Games/Army Men II" build/armymen2 -nointro

Needs `glibc-devel.i686`, `libstdc++-devel.i686` and `SDL3-devel.i686`. It is
32-bit and cannot be otherwise: the carried `.data` holds 32-bit pointers and
every record offset assumes a 4-byte pointer. The section placement is the
standalone's -- `.origdat` at the original's VAs, our code at `0x00700000` --
and the same generated files serve both builds; only the assembler section
flags differ by object format, and `mkglobals.py` emits both.

**What it reaches, measured on Xvfb `:99` with real X input (`xdotool`):**

| step | evidence |
|---|---|
| title screen | **54 of 307,200** pixels differ from the injected build's title frame (`build/shots/99/title.png`), all inside the cursor's 10x13 box |
| BOOT CAMP, briefing, both dialogs | `lines: 101  tokens: 372  names: 43  compounds: 16` -- the four totals CLAUDE.md records for Boot Camp |
| live mission | `tools/objdump.py --table` reads **1,609** objects over the control socket; holding W moves Sarge (`pos=1979,1028` after 2.5 s from the start) |
| QUIT through the menu | `Releasing Comm Connection`, `Unreleased memory (0) blocks`, `Packet Thread Exited with return code 259`, `Receive thread got event 0`, exit 0 -- the same lines as the Wine log, 259 included |
| the game's own log | 14 messages on a Boot Camp run, the count the Wine A/B table gives, with no line the original does not write |

The layout of `src/platform/` is in its `platform.h`. Four things decided there
that are worth knowing before touching it:

- **DirectDraw surfaces are plain 8-bit buffers.** The primary's pixels go
  through its attached palette (or the last realised GDI palette) into one
  streaming SDL texture on Flip, on any Blt into the primary, on Unlock or
  ReleaseDC of it, and on a SetEntries of its palette -- that last one is
  what makes the game's palette fades show without a flip.
- **Exclusive modes are accepted and not enforced.** `SetCooperativeLevel`,
  `SetDisplayMode` and DirectInput's exclusive mouse all answer success and do
  nothing; the game gets a window whose logical size is the mode it asked
  for, and the desktop keeps its pointer.
- **GDI text is stb_truetype** (`src/platform/stb/`, vendored, public
  domain), drawn onto the surface a DC names and thresholded to one palette
  index, which is what the game's `EncodeGlyph` reads back. ArialNarrow is
  Liberation Sans at 82% width; ArialBlack is Liberation Sans Bold.
  `AM2_FONT_NARROW`, `AM2_FONT_BLACK`, `AM2_FONT_DEFAULT` override.
- **The game's cursor is kept under the host pointer by measuring each delta
  from where the game's cursor actually is**, not from the last pointer
  position; `runtime.cpp` installs the query. And each axis is followed by a
  zero on the same axis, because `PollMouse` re-adds its sticky per-poll delta
  after EVERY event -- which is the "acceleration" this file's Wine notes
  measured, explained.

**A sixth, found by walking:** `ScrollView` blits the offscreen surface
onto itself, and a copy that walks rows top-down reads rows it has just
overwritten whenever the destination lies below the source. With a
one-pixel scroll every row becomes a copy of the first, which showed as
vertical stripes over the whole map the moment Sarge moved or the pointer
scrolled the view. The copy goes bottom-up in that case now; verified by
walking in all four directions and edge-scrolling across the map.

**A seventh, reported as broken collisions:** a DirectDraw Flip blocks
until the vertical blank, and the game's pace is its frame rate -- how far
a trooper steps and how far he turns are per frame. My Flip returned at
once, so on Xvfb's software GL the game ran at whatever rate a present
cost, the steps grew, and Sarge walked straight through sandbags and the
hut. `am2_host_wait_vblank` paces Flip at 60 Hz now (`AM2_FPS` overrides,
0 disables), and the same drive on both builds -- turn 1.2 s, walk 8 s --
ends at (1246,1227) here against (1247,1205) under Wine, stopped by the
hut in both. Measured, since the object tables at mission start were
already identical apart from Sarge's own row.

**An eighth, and this one is in the RECONSTRUCTION, found by the native
build's first run on a real desktop:** `Type2PlayerStep` seeded the AI
context's class at +4, and the original seeds +0 -- the store is
`mov [esp+0x14], eax` at 0x0044AD51 while the `push edi` of the call before
it is still on the stack, so it is [esp+0x10], the context's first field,
once `add esp, 4` has run. `AiTrooperStep` indexes its move-state table
with +0, which the wrong reconstruction left uninitialised: small enough to
pass under Wine for months, a libc pointer on the desktop, a fault. Settled
by measurement, not reading: the original run with only `AiTrooperStep`
replaced by a logger writes 0, the class, at ctx[0] on every call from that
site, and garbage at ctx[1]. No A/B could have seen it -- both sides of an
injected comparison run the reconstruction -- and it is the same cdecl
cleans-later trap CLAUDE.md records for `CreateVehicle`.

**And the collision report was FOUR defects in the reconstruction, none
of them visible under Wine.** "Sarge walks through things" was chased by
dumping the two collision planes -- `ADDR_CELL_WEIGHTS` and
`ADDR_TILE_COVER`, 256x256 bytes each -- out of a live Boot Camp mission and
diffing them against the ORIGINAL's under `AM2_NOPATCH=1`. The injected
build is not the oracle there: both sides of an ordinary A/B run the
reconstruction, which is why all four had survived every configuration in
the suite. `AM2_NOPATCH_NAMES=a,b` (a comma list of reconstructions left
original) bisected them one at a time:

- `LoadMask` had the `-df` polarity inverted, so the packed masks were
  loaded only under the switch and no drive here ever loaded a footprint
  mask at all. CLAUDE.md already records that switch's name says the
  opposite of what it does; this was the same trap one function over.
- `ObjAfterMove` and `ItemTeardown` took the cell index from the wrong
  slot -- `[esp+N]` read before a push rather than after it, the eighth
  defect's shape again -- so a walking object's own weight was moved to a
  cell chosen by a rectangle coordinate.
- `ObjAfterMove`'s remove arm takes TWO off the centre cover cell
  (`add dl, 0xFE` at 0x0043922D) where the add arm and both of
  `ItemTeardown`'s take one; the shared helper had smoothed the asymmetry
  away, and `ShiftTileCover` carries the centre's delta separately now.
- `ObjFootprint` and the roach pair indexed `ADDR_CELL_WEIGHTS` as the
  plane, where it is a POINTER to the plane (`mov edx, [0x514EC0]` at
  0x0045A70B). Every vehicle footprint -- laid and lifted each frame by
  `Step3Drive` -- went into the globals after 0x00514EC0, so no vehicle
  ever blocked anything.
- Not a defect but a cause of two false ones: the native build's random
  seed was a private global, so anything randomised diverged from the
  original run to run; it reads `ADDR_RAND_SEED` now.

**AND THE PLANES WERE NOT THE DEFECT THE REPORT WAS ABOUT.** With both
identical, Sarge still walked through Boot Camp's hut, and so did the
INJECTED build under Wine -- the same drive on the original stops him at
(1653,1145). Bisecting `AM2_NOPATCH_NAMES` with "does the hut stop him" as
the oracle needed the whole by-name chain left original, WinMain down to
`UpdateTrooperAction`, because a reconstruction reached by NAME cannot be
put back one function at a time; greedy removal then left exactly that
chain, so the defect was in that function's own body. Three, all in
`UpdateTrooperAction` and none an offset:

- The move was handed `OBJ_OFF_FIELD_44` itself where the original hands
  the SPEED slot whenever that field is set, and the trooper's own signed
  height only when it differs from the tile's. The field reads 88 on a
  walking Sarge, `ObjMoveAlongFacing` made that his height, and
  `BlockWeightRoute` discounts anything more than 16 units off the walker's
  height -- so every hut and sandbag weighed nothing.
- The no-route chain fell through to the settle with `turned` clear when
  none of its four stop conditions held; the original's failed multiplayer
  tests all jump back to the heading sweep.
- The sweep tried one heading too many (its count starts at 1), passed the
  state where the original passes the saved pose, and picked the player's
  shorter limit by comparing Field548's VALUE against the default owner
  instead of the object's army byte -- the idiom CLAUDE.md records.

Verified by probing the callees the original reaches by address: with the
original `UpdateTrooperAction` in place and ours, the per-frame sequence at
the hut is the same -- three-heading sweeps every frame, all blocked,
`PickFireMode` each time, the same facing distribution over 4,900 frames.
No configuration in the suite can see this class: both sides are driven
with the same input and nothing compares a position in play.

Both planes are BYTE-IDENTICAL to the original's with the HQ dialog up.
Dumped in live play they differ by a handful of cells, which is the socket
thread reading between a vehicle's clear and its re-stamp; take that dump
under a dialog, where no frame composes.

**Five defects the first native run found, each a Windows behaviour the
reconstruction relied on without saying so:**

- `_strlwr` writes only the characters it changes (MSVC's does); `map.cpp`
  lowercases the literal `"camera"`, which lives in `.rodata` here.
- Text-mode `fopen` strips CR before LF; `mpmaps.txt` read raw yields a
  `"\r"` token on every blank line and the level list does not parse.
- `crt.cpp`'s host `chdir` default does not understand a backslash; every
  `SetGameDir` before `am2_crt_use_game` failed silently and the `.aai`
  files opened from the wrong directory.
- The packet thread is cdecl behind a stdcall cast (dplay.cpp records the
  original's mismatch), so the thread trampoline restores `esp` itself.
- `IID_IDirectPlay4A` and `IID_IDirectPlayLobby3A` were compared against the
  wrong GUIDs, the lobby was never created, and `CommLobbyStart` calls a
  method on the NULL that follows -- the original's test there "can only
  ever pass", so it has no path for that.

**TWO NATIVE BINARIES: THE PLAYER'S AND THE DEVELOPER'S.** `make native`
builds `build/armymen2` with no control socket, no injected input and no
savestates; `make native-dev` builds `build/armymen2-dev`, the same sources
with `AM2_DEVTOOLS`, plus the harness's control.c and input.c and
`src/standalone/devtools.cpp`. The socket is on by default there (port
31337, `AM2_CONTROL=0` turns it off), and everything a drive or a dump
needs lives in that binary only.

**SAVESTATES, in the dev binary.** Every game allocation there comes from a
96 MB arena at a fixed address (0x0A000000) with a deterministic first-fit
allocator whose bookkeeping lives inside the region, so a savestate is the
carried globals (2 MB, 0x0046F000..0x00666000) plus the arena's used part,
16 MB in a Boot Camp mission, written and restored as bytes.
`snap save FILE` and `snap load FILE` on the socket (FILE absolute -- the
game chdirs), F5 and F9 for `$TMPDIR/am2-quick.state`. They run on the game
thread at the top of the host's frame pump, never mid-frame. Measured: save
at (1981,1026), walk to (2249,998), load, read (1981,1026), walk again and
the game carries on. What a state does NOT hold is anything the platform
owns -- surfaces, sound buffers, open files -- so it is valid within the
session and mission it was taken in; the game's own SAVE GAME is the
portable one.

**THE ARENA'S ADDRESS IS NOT A FREE CHOICE.** The game overloads fields
with a uid or a pointer and tells them apart by value; uids carry their
kind in the high nibble (`200003E8`, `800003E9`), so the arena has to sit
where the MSVC heap and glibc's brk heap both do, below 0x10000000. Three
bisecting switches stay in: `AM2_DEV_NOARENA=1` (libc's allocator),
`AM2_DEV_NOREUSE=1` (freed blocks never handed out again),
`AM2_DEV_POISON=1` and `AM2_DEV_CANARY=1` (freed blocks filled with 0xDD
and checked every frame, naming a block written after its free by size and
allocating call site). They exist because a campaign start came up as a
black screen in three arena runs in a row and in none of four non-reuse
runs; the canary found no write into freed memory, and the next four runs
under every mode came up fine. That start is intermittent and unrelated to
the allocator -- the mission is sometimes seen in play at sub-state 0x21
with nothing drawn and the briefing never shown, and a SPACE brings it up.
Not understood, and said so.

**A LEVEL CAN BE ENTERED AT WILL, FROZEN, ON ANY BUILD, IN FIFTEEN SECONDS
AND WITHOUT A MENU, and that is the verification point the port needed.**
The game's own save is the snapshot that crosses runs and builds, and a load
of it is how a level is entered. Two harness pieces make that a fixed point:

- **`loadgame FOLDER FILE` on the control socket** makes the LOAD button's
  four writes (`OnLoadGameLoad`, 0x00452060): the folder name into the
  game-proc block, the file name into its second string, the load-pending
  flag, a request for state 2. No menu, no coordinates, and it takes
  `AM2_NOPATCH=1` unchanged because all four are the game's globals.
- **`AM2_PAUSE_ON_ENTER=1`** raises the -dbg pause reason the moment the
  game state becomes 2, which the game does on a fresh start and not after
  a load (its pause is gated on no load pending). The hook is in
  `input_pump`, on the game thread in every build. The mission arrives
  frozen before its first frame.

`tools/enterlevel.sh [-b dev|orig|recon] [-f FOLDER] SAVE` copies the save
into `save/FOLDER`, starts the build, issues the load and prints the object
count and the table's md5, leaving the game paused on its port. Measured:

| save | build | objects | table md5 |
|---|---|---|---|
| campaign, MAP 01 | dev, twice; original, twice; injected | 327 | b1ced1edb769 |
| Boot Camp | dev, original, injected | 1,612 | a9ee8b48d784 |

**BOOT CAMP CAN BE SAVED, which the menu never offers.** `SaveGame`'s
guard is the game-proc block's first string being non-empty, and that
string is the FOLDER the save goes under: the player's name in the
campaign, and `bootcamp` in Boot Camp, where the block already holds it.
So the save dialog, opened by writing the two globals the SAVE button
writes, saves Boot Camp's briefing into `save\bootcamp\` -- 1,609 items,
347 KB -- and `loadgame bootcamp FILE` at the title screen brings it back
identically in all three builds. The raw savestates do not cross runs: a
state saved in one run and loaded in a fresh run at the same point of the
same mission dies in `RestoreLostSurfaces` on the first frame, address
randomisation off or on, because the platform's surfaces are at new
addresses. They stay the quick tool within a session.

**`tools/savecheck.sh` IS THE SERIALISATION A/B, and it passes.** The
game's save file is the one snapshot that crosses builds: objects go out
by uid. The script drives the dev binary and the ORIGINAL under Wine to the
same campaign briefing, held under `-dbg`, has each save through the game's
own dialog (opened by writing the two globals the SAVE button writes, the
name typed after clearing the pre-filled one), then loads each file in each
build and freezes the loaded game with the game menu. Measured:

| comparison | result |
|---|---|
| the two saves' object tables, 325 objects | static fields identical; two walking troopers 1 px apart |
| native's file loaded by the original and by native | identical in every field, frozen on entry |
| the original's file loaded by both | identical in every field, frozen on entry |
| each build's load against its own save | static fields identical, with the four counter-uid'd type-6 records renumbered by both |
| the two files, 180,618 bytes | 2,943 bytes differ, all of them accounted for |

The file bytes cannot match and the tool says why per section: the
object-script, condition and item sections write records whole, pointers
included -- native's sit in the arena and Wine's in its heap -- the
game-proc block carries the clock and a frame counter, the event block
three counters two frames apart, and one word of the script section is
padding the arena zero-fills. `SaveObjScriptSection`'s
own comment had already said the item section cannot be compared byte for
byte across builds. The one-pixel offset is the native port's first frame,
not the reconstruction: the injected build under Wine gives a briefing
table identical to the original's, and a second native run gives one
identical to the first.

**DIRECTSOUND IS IMPLEMENTED, and checked without ears.** A secondary
buffer keeps its bytes; one mixer, pulled by the host device from its own
thread, walks every playing buffer's cursor at the buffer's rate,
resampling 8/16-bit mono/stereo to float stereo with the volume and pan
applied, so Lock/Unlock write into the very memory the mixer reads,
GetCurrentPosition advances in real time and a non-looping buffer stops at
its end with the cursor back at 0 -- the semantics the stream refill and
the effect restarts were written against. The 3D listener is accepted and
ignored: the game does its own distance attenuation. winmm's mmio is the
buffered RIFF reader the wave loader consumes directly through
GetInfo/Advance/SetInfo, and the multimedia timer is the host's.

`AM2_AUDIO_DUMP=<file>` writes everything the mixer hands the device as raw
stereo float, and that is the check: a Boot Camp drive under
`SDL_AUDIO_DRIVER=dummy` dumped 41 s, and seconds 2..6 of it cross-correlate
with `title.wav` resampled the mixer's way at **1.0000, zero error**, at a
gain of 0.355 -- the game's own music volume -- and at a lag of exactly
2.000 s, so the stream's refill kept time to the sample over the run. The
mission's effects show as bursts in the RMS trace. `AM2_NOSOUND=1` answers
DSERR_NODRIVER, the silent path the original takes with no device.

**DEFERRED, deliberately:** DirectPlay (objects that decline everything, no
transport) and Smacker movies (`SmackOpen` answers NULL). Present does
not wait for the display's vertical blank by default (`AM2_VSYNC=1` turns it
on): the clock already paces Flip, and a compositor throttling an unfocused
window to a frame a second reads to the game as enormous time steps.
Windowed mode
(`-w`) renders the same title frame -- 208 pixels differ from the reference
in both modes, all of them the cursor, drawn at its start position here and
where the A/B's drive left it there -- and it needed one more Windows fact:
a windowed primary shows through the system palette GDI realised, not
through the DirectDraw palette attached to it, which the game builds from a
snapshot that is still zero at that point. The A/B suite does not know this
build exists yet.

## `AM2_GAMEDIR` points the port at the original game's directory

The original has no way to be told where the game lives: `CheckBasePath` reads
the working directory into `ADDR_GAME_DIR` and every `SetGameDir` concatenates
onto that, so the install directory is simply wherever the process started.
Fine for a shortcut in the game folder, useless for running the port from a
build tree or aiming it at a second install.

    AM2_GAMEDIR='C:\GOG Games\Army Men II' wine .../am2port.exe -nointro

Set it and the process chdirs there before the read, so everything downstream
sees that directory. **Unset it and nothing changes** -- with the variable
absent `CheckBasePath` is the original's function instruction for instruction,
which is what keeps the A/Bs honest, since both halves are driven with the
same environment.

Verified in both directions from a scratch working directory:

| | log | screen |
|---|---|---|
| with the variable | the normal four startup lines | 234 distinct colours -- the title art |
| without it | `Unable to load wave file click.wav` | 2 colours -- blank |

A chdir that FAILS is logged, naming the directory and the Win32 error, and is
not fatal. `FatalError`'s only string is about a path being too long, which
would be a lie; a game that cannot find its data then says so in its own words
with our line above it.

**WHAT IT BUYS: the port runs from the BUILD TREE, never copied anywhere.**

    AM2_GAMEDIR='C:\GOG Games\Army Men II' \
      wine explorer /desktop=NAME,1024x768 \
      'Z:\home\boran\git\ArmyMen2\build\ArmyMen2.exe' -nointro

Verified: the title art renders (234 distinct colours) and the log is the same
four startup lines. It lands beside the EXE, so `build/am2port.log` rather than
the game folder's -- which is the diagnostic that memory note asks to keep, and
it follows the binary rather than the data.

That removes the install step from the edit-run loop entirely. The game folder
keeps the ORIGINAL `ArmyMen2.exe` untouched, which every A/B needs, and
`am2port.exe` beside it stays useful for testing what a player would actually
have.

## OPEN: the trooper "jerksteps" while turning -- a REPRO, and what it rules out

Reported from play: in Boot Camp hold W to move forward and then hold A (or S)
-- Sarge should run in circles without stutters, and does not. That is a
deterministic repro and far better than anything the suite can drive; it also
needs no mouse, so the input is exactly two held scancodes.

**Drive it with `ctl key 0x11 down` and `ctl key 0x1E down`** -- the command
takes a NAME or an `0x`-prefixed scancode, and a bare `11` silently resolves to
nothing, which reads as the game ignoring input. `ctl keys` confirms what the
GAME sees: `down: 11 1e pressed: 11 1e`.

**Walking straight is NOT broken**, measured on both sides under the same
drive with a matched `Options.cfg`. Both cycle the same animation chain --
frames 0x2E, 0x04, 0x2F, 0x05 through `ROW_OFF_ANIM_NEXT_ID` -- and both
advance position steadily, about 0x23 a sample.

**What the turn quantisation is NOT:** our `(facing +/- 0x10) & 0xF0` looked
like a prime suspect -- 16 directions, a whole notch a step -- and the original
does exactly the same, `sub al,0x10; and al,0xf0` at 0x0044A566 and
`add al,0x10; and al,0xf0` at 0x0044A5BC. Stepped facing is the game's own.

**IT IS OURS, AND HERE IS THE MEASUREMENT.** Forty samples a side, same
drive, same matched `Options.cfg`, the same injected `key 0x11 down`:

| | animation cell | animation frame |
|---|---|---|
| original | 00 x19, 01 x15, 02 x6 -- it ADVANCES | 04, 05, 2e, 2f -- the whole walk cycle |
| ours | **00 x40 -- never advances** | **01 x26 (STAND), 05 x14 (walk)** |

The original never shows frame 1 while walking. Ours flips between stand and
walk, and every flip calls `SetAnimFrame` with a different frame, which resets
`ROW_OFF_CELL` to 0 -- so the legs restart perpetually. That is exactly the
reported symptom, and the cell never advancing is a CONSEQUENCE of the frame
changing, not a second bug.

So `ActionKeyDown(0)` reads FALSE on frames where the key is held. The harness
is not the cause: both sides get the same injection through the same hook, and
only the reconstruction flickers.

**Excluded by reading the original beside ours:** `ActionKeyDown` itself (the
original loads the buffer pointer once and tests both bindings; ours does the
same), the key bindings (byte-identical), `PollKeyboard`'s buffer SWAP (same
three moves, same order, GetDeviceState into the new current), and its
`DI_OK`-is-zero retry.

**AND THE INPUT IS NOT THE CAUSE EITHER.** An 11-arm probe over every write to
the action inside `Type2PlayerInput`, plus both writes inside
`Type2PlayerStep`, run on a verified-live drive with W held:

    7741  arm=1    1 -> 2      the ActionKeyDown(0) walk arm, EVERY frame
     155  arm=102  1 -> 2
       1  arm=101  2 -> 1

So `ActionKeyDown(0)` answers TRUE every frame -- the key state is fine, and
the flicker is not a missed keypress. What the counts say instead is that the
field reads **1 at the START of nearly every frame**: the walk arm would fire
once and stay if nothing reset it, and instead it fires 7,741 times.

Neither instrumented arm puts a 1 there -- arm=101 is the only 2 -> 1 and it
fired ONCE. So the per-frame reset happens OUTSIDE both functions, in
something else holding the same record: it is `o + OBJ_OFF_SIGHT_OUT_T2`, and
`AiTrooperStep` and `TrooperFire` are both handed it.

**Excluded, each by reading the original beside ours or by measurement:**
`ActionKeyDown`, the key bindings, `PollKeyboard`'s swap and its DI_OK retry,
`KEY_STATES` being 256 so the harness overlay applies, the wheel delta being
cleared each poll, and the turn quantisation.

**FOUND: `UpdateTrooperAction`'s `no_route` ARM, firing every frame.**
Instrumenting all TWENTY-FOUR writers of that field in region.cpp and running
the repro on a clock-verified live drive:

    7838  arm=14  1 -> 2   Type2PlayerInput's ActionKeyDown(0) walk arm
    6197  arm=12  2 -> 1   UpdateTrooperAction, the `no_route` block
     156  arm=8   1 -> 2   Type2PlayerStep
       1  arm=7   2 -> 1

So the two fight every frame. The input says walk, `UpdateTrooperAction`
decides there is nothing walkable ahead and stops the trooper, and the next
frame the input says walk again. Each flip calls `SetAnimFrame` with a
different frame, which resets `ROW_OFF_CELL` to 0 -- the legs restart, and the
movement hitches. That is the whole symptom.

**So the defect is in the walkability test**, in the code above `no_route:`
that decides whether the tile ahead can be entered -- not in the input, not in
the animation, and not in the key state. `arm=12` also writes 1 over garbage
values (`28 -> 1`, `7796576 -> 1`), so it runs for other objects too and the
test is shared.

**FIXED, AND THE BRANCH SENSE WAS SETTLED BY CONTROL FLOW RATHER THAN BY THE
LOCAL.** `< 15` is CLEAR. Three things say so together and none of them needs
to know what any stack slot holds:

- The sweep loop at `0x0044B283` is `jge 0x44b208`, which loops BACK. A sweep
  continues while blocked, so `>= 15` is blocked and `< 15` ends it.
- `0x0044B14B`'s `jl` target, `0x0044B382`, writes the facing back and falls
  into the settle-and-step at `0x0044B41C`. It runs no stop arm at all.
- The `>= 15` fall-through at `0x0044B151` reads the claimed vehicle uid and,
  when there is none, `0x0044B159` jumps to `0x0044B299` -- which IS this
  function's stop block, calling `ObjKind538In10To17` and writing `w[8] = 1`.

So the original reaches the stop arms only when the step is BLOCKED and no
vehicle is claimed. Ours reached them when the step was CLEAR.

The one piece of evidence that had held the fix up -- `mov [esp+0x1c], 0` on
the `jl` path -- turned out to discriminate nothing: the give-up arm at
`0x0044B3A7` and all four stop arms zero the same slot. A local that is zeroed
on both sides of a branch cannot say which side is which, and treating it as
though it could is what made this look unreadable for two sessions.

**The function's own comment already knew.** The header says of the sweep
"under AM2_STEP_ROUTE_OK is walkable and ends it" -- correct, and the exact
opposite of what the first gate twelve lines below it did with the same
comparison. The sweep's copy of the test was right all along; only the first
one was pointed at the wrong label.

Measured on a live Boot Camp mission, holding W, sampling the leader's
`OBJ_OFF_POS` once a second:

    ours      x += 86, 91, 92, 91, 92     y -= 9, 10, 9, 10, 9
    original  x += 92, 91, 91, 65, 91     y -= 9, 10, 10, 18, 10

and holding W and A together, which is the repro Boran gave -- Sarge should run
in circles -- both sides trace a circle of the same size, 20 units across in x
and 18 in y, over the same twelve samples. Before the fix he restarted his walk
every frame.

**`combat`'s frame gate fails and it is NOT this change.** 16505/44385 and
16592/43432 on two runs of the fix; the PARENT commit, same configuration, same
machine, reads 16591/50865 -- worse. So the gate was already failing and the
fix NARROWS it, from 306% to 261%, which is what our side doing the same work
the original's does should look like. The control was run because this file
says a single clean control does not establish a regression; here it
established the opposite, which is the same tool used the other way round.

Everything else is clean: `bootcamp` state identical at 1,610 objects,
`campaign` at 2 pixels and 35 widget nodes, `mission` with state, widgets and
log identical and the original as the runaway in its usual direction.

**A parser bug wasted three runs here and is worth naming:** `ctl dump` returns
ONE contiguous hex string, not space-separated bytes. Splitting it into tokens
and indexing gives an empty result, which reads exactly like a dead socket --
and the socket was answering `pong` throughout.

**Do not read an earlier "cell stuck at 0" observation as evidence** -- it came
from a run with stale instances alive, the condition CLAUDE.md warns produces
convincing false results, and it did not reproduce once the environment was
clean.

## In flight

Nothing uncommitted. **1,643 patches plus 6 REGISTERED**, **61** analysis
tools in `make check` (`tools/checkpatches.py`; `tools/checkclaims.py` counts
the recipe).

## THE STANDALONE PORT REACHES THE MAIN MENU

`make standalone` builds `build/ArmyMen2.exe`, which replaces ArmyMen2.exe in
the game folder and needs the original at BUILD time only. It renders the
title art, the six menu buttons, the cursor and the copyright text, and its
log is identical to the injected build's, line for line.

    make standalone      # -> build/ArmyMen2.exe, copy into the game folder

How it is put together, and why: the image's relocations are stripped, so the
3,582 pointers inside its own data cannot be told from the bytes around them
and cannot be moved. `.origdat` is OUR section, placed at the addresses those
pointers were written for, with our code linked above at 0x00700000. What is
rewritten at startup is what IS unambiguous -- 411 function pointers, the
whole import table (171 across 10 DLLs), and the 21 C++ static initializers
the MSVC CRT used to run.

**Every table migrated out of that blob into typed C data is one less thing
holding the layout in place**, and the first is already gone: `c_dfDIMouse`
now comes from the DirectInput SDK rather than the image.

**The blob is smaller than it looks, which reframes that campaign.** Of its
1.96 MB only **73,772 bytes are non-zero**, the last at 0x0048D8D3 -- MSVC
folds `.bss` into `.data`, so 94% was never in the original's file either. It
is split at 0x0048E000 now, with an ALLOC-only `.origbss` for the tail, and
`build/ArmyMen2.exe` is 4,169,345 bytes rather than 6,102,591. What actually
holds the layout in place is 124 KB, not 2 MB.

**Two tools check the port rather than leaving it to a screenshot.**
`tools/samission.sh` is the stronger: it drives both builds into the same
live Boot Camp mission and diffs the whole object table with no budget --
1,610 lines, identical -- which is the artifact ab.sh already treats as its
sharpest. `tools/samenu.sh` is the cheap one and covers startup:: it runs both builds through the same startup and compares the
game's own log (identical, five messages) and the title screen (0 of 307,200
pixels on a run where the cursor lands in the same place, 45 when it does
not). Tested in the failing direction with a negative budget.

Past the menu the port loads and plays a Boot Camp mission, and its LIVE
OBJECT STATE is identical to the injected build's -- all 1,610 lines of
`tools/objdump.py --table`, diffed with no budget, taken in ordinary play at
sub-state 0x21. That is the same artifact `ab.sh bootcamp` treats as its
sharpest, and it is a much stronger statement than the title screen.

A report that Sarge cannot be moved did not reproduce: with the cursor placed
absolutely through the socket, the same click moves him in NEITHER build, so
the click is not a move order and the port is not diverging.

## NOTHING LEFT TO TRANSPOSE

`tools/remaining.py` reads **0 game functions and 0 C++ static initializers**
over every byte from `0x00401000` to the real CRT frontier at `0x00464420`,
and reports that the entries tile `.text` with no gaps.

**The standalone build is a stronger completeness oracle**, and it found
three things nothing else could see -- `0x0040A6A0`, `0x004185C0` and
`0x00451990`, called by address and still the original's code, all INTERIOR
addresses of merged entries whose entry is patched, so every count read them
as done. All three are closed now: the first is a linker thunk to
FreeArmyObjLists, and the other two are reconstructed as `OnEnterNameOk`
(RECRUIT's ENTER NAME dialog) and `HudChatChar` (the HUD chat WM_CHAR
handler). It also found `0x00433770`, which is not a function at all but a
byte table MSVC placed in .text, now extracted into generated C.

Nothing in the standalone build is stubbed any more except the
AM2_PROBE_NOACTION seam, which exists to call the ORIGINAL action parser and
so cannot mean anything in a build that carries none of it.

## THE PLAYER CAN MOVE AGAIN

`StepType2`'s player-input gate was transcribed as a test for null where the
original does `cmp [0x5122c8], esi; jne` -- is the FOLLOWED OBJECT THIS ONE.
The camera follows Sarge all mission, so the original took that arm every
frame and we took it never: no key and no click moved anything, in either
build. Reported from play, invisible to the whole suite.

Nothing here could see it, and each artifact has its own reason. The A/B
drives both sides with the same input, so they agree about ignoring it;
`bootcamp`'s object dump is taken at the briefing before anything moves;
`mission`'s pixel check is off by construction; and every counter on that
path is blind, its callers being ours.

**`tools/movecheck.sh` is the check that was missing** -- the only one in the
project that drives a REAL DEVICE, sending X events through `xdotool` so they
reach the game as a player's keyboard does. It asserts DISPLACEMENT rather
than equality, since two unsynchronised runs walk for different lengths of
time. Mutation-checked by putting the bug back: 0 units against the
original's 237, and 241 against 237 with the fix.

    tools/movecheck.sh
    tools/saquit.sh        # the standalone's teardown, which nothing covered
    tools/loadcheck.sh     # loading a save; FAILS today on the known defect

Generalising the defect into a static check was tried and abandoned with
measurements -- see CLAUDE.md. MSVC compiles a null test AS a register
compare, so all 15 candidates it finds are correct.

## THIS SESSION'S CHANGES ARE VERIFIED ACROSS SIXTEEN CONFIGURATIONS

Sixteen `ab.sh` configurations were run over the four `src/` changes this
session landed. Fifteen are clean:

    bootcamp campaign combat mission quit controls windowed menuscreens
    intro audio df multi difficulty audiovol movies state3

`mpoptions` fails -- and it was ALREADY failing before this session, with the
same signature to the pixel: commit 2f55eb3 of 2026-09-03 records it logging
"Couldn't open bitmap file!" twice on the reconstruction side and reporting
**221,423** differing pixels, which is exactly what it reports today, twice
over. So it is a standing defect on the multiplayer options screen and not a
regression from anything here.

## THIS SESSION'S CHANGES ARE VERIFIED ACROSS EIGHT CONFIGURATIONS

Four `src/` changes landed -- the player-input gate, `LoadGameProcSection`'s
two missing stores, `State2Enter`'s missing no-argument log call, and the one
deliberate deviation in `LoadItems`. They are checked by:

    bootcamp  campaign  combat  mission  quit  controls  windowed  menuscreens

all clean, plus `loadcheck` (now passing on both builds), `movecheck`,
`samenu`, `samission` and `saquit`. `mission`'s frame gate fails as it has on
every run for reasons recorded in CLAUDE.md -- ours at 635, inside the
documented band -- while its state, widgets and log are identical.

## LOADING A SAVED GAME WORKS

`tools/loadcheck.sh` passes: our build and the original both come back alive
at sub-state 0x18 with 325 objects and the save file untouched. The STANDALONE
does the same, measured separately: alive, sub-state 0x18, 325 objects, md5
unchanged, `Loaded 317 items` in its log. Two fixes and
one deliberate deviation got there; see CLAUDE.md for all three.

The deviation is the only one in the tree: `LoadItems` unlinks objects from
the cell grid before `ItemsReset` frees them, because the original leaves 305
dangling entries and survives only because its allocations happen to reoccupy
them. It is confined to the load path so no other configuration is touched.

## FIXED: LoadGameProcSection dropped the two stores that make a load happen

The original ends that function's success path with `HAVE_DEFAULT_COF = 0`
and `LOAD_PENDING = 1` (0x0042698B). We had neither. They exist because the
function's own fread overwrites LOAD_PENDING -- it lies 0x370 into the
0x438-byte block being read -- so without them every load silently became a
fresh start, and the retry stamp in MissionStartup then saved over the file
the player asked to load.

After the fix our build logs `Loaded 317 items` for the first time and the
save's md5 is unchanged; `ab.sh bootcamp campaign` is clean.

**It exposes the next defect**: the load path had never executed here, and now
that it does, our build exits just after `calculating region data...` where
the original loads and lives. That is the next thing to chase.

## SUPERSEDED: our autosave fires where the original's does not

**Retracted first**: an earlier entry here said the standalone loses saved
games. It does not. `drive.sh` defaults to `-nointro -dbg` and my standalone
launches passed `-nointro` alone, and the pause that decides the whole
behaviour is gated on that switch. With the arguments matched the standalone
loads a save exactly as the injected build does -- sub-state 0x21, the save's
316 objects, md5 unchanged.

What survives is real and is not standalone-specific. Without `-dbg`, OUR
reconstruction rewrites the save through `MissionStartup`'s retry stamp and
the ORIGINAL does not, in both builds. Starting the mission fresh without
`-dbg` is the game's own behaviour; the extra save is ours.

The three guards are transcribed correctly against the disassembly, so the
divergence is whether the call is reached -- `TakeMenuRequest`, entered from
`State2Frame` on arm 11 only when `GetPauseFlags()` is zero.

No configuration in the suite can see this: they all run with `-dbg`. It is
the player running the port normally who is affected.

## Where the work is now: VERIFICATION, not transposition

Everything is transposed; not everything is checked. The sharpest statement
of the gap is CLAUDE.md's list of functions no drive in this environment
reaches, which `tools/checkclaims.py` splits by measurement rather than by
assertion. **That list is now 31 of 32**, and the one remaining is excluded
deliberately rather than outstanding: `ItemSetBox`, whose arithmetic is
`RowAlloc`'s and runs 2,512 times a mission, so it has live coverage and only
this transcription of it does not.

Each of the thirty-one is an enumerating oracle of the `tools/shakecheck.py`
shape, because no configuration here reaches any of them and none can be
added -- Boot Camp's enemies never engage, and MAP 01 turns hostile the
moment its dialog clears, which is the one screen whose log, pixels and
object table are all unavailable at once.

Three lessons from closing the last of them are worth carrying to the next
oracle, all recorded in CLAUDE.md:

- **A stub has an ABI.** `tools/vehexitcheck.py` reported 71 of 85 cases
  differing with the ARM SEQUENCE CORRECT on every one and only the arguments
  wrong, which is the signature of a stack off by a push -- two of its ten
  callees are thiscall.
- **A corpus derived from the model cannot fail against it**, in a new place:
  `tools/tilesetcheck.py`'s model looped over the case's chunk list where the
  original loops on `offset < formSize`, so removing a payload from the
  accumulation passed all 84 cases.
- **Diff before leaning on a sibling for coverage**, not only before merging.
  `LoadAtlFile` runs on every map load and reads the same file format as
  `RestoreTileSet`, but at similarity 0.245 with no shared run of six it
  covers the FORMAT and none of the instructions.

## Where the boundary is

The sub-CRT set is **closed at 1,239 of 1,239**, and so is everything above
the nominal `CRT_START`: `tools/crt.py` measures **148 game functions** below
the real frontier -- 112 of them above the nominal line, code every count in
this project silently dropped until that tool existed -- and reports
**0 unlabelled**. The Win32/DirectX boundary is separately closed at 0 COM
sites and 2 unreachable `MessageBoxA` sites (`docs/boundary.md`).

The line itself is measured rather than assumed: game code runs to
`0x00462600` and the CRT proper starts at `0x00464420`, with a six-entry
thunk table parked between them.

## Families closed earlier in this run

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

## `tools/ab.sh all` -- 17 configurations, one failure and it is the known one

Run end to end after this session's work. Every configuration's real evidence
is identical: logs match on all seventeen, every widget tree matches
(`mpoptions` 131 nodes, `campaign` 35, `controls` 26, `mission` 16,
`audiovol` 14, `movies` 9, `multi` 9, `menuscreens` 8, `difficulty` 7), and
`bootcamp`'s 1,610-line object dump and `mpoptions`' five exact state dumps
both match byte for byte. Nine configurations report ZERO differing pixels.

The one failure is `mission`'s frame gate at **26157/640**, and CLAUDE.md
already says what it is: our side's band went stale at roughly 600 against the
original's 26,000, so that gate fails on every run and "the failure is not
evidence of anything". `mission`'s own evidence -- state, 16 widget nodes, 13
log messages -- is identical.

Read `intro`, `mission` and `combat` pixel figures as meaningless by
construction; their logs are the evidence and all three match.

## CLOSED: `ab.sh mpoptions` is A/B clean -- widgets, state and log all identical

**Our build EXITS when the multiplayer options screen is requested.** It is
not a drawing difference and never was: 221,423 pixels is one side showing a
panel and the other showing whatever was on screen when the process died. The
drive's own symptom said so all along -- "could not settle the team button
(got )" is an empty widget dump, which is what a dead game returns.

Driven identically by the same pokes, under `AM2_WINE_OUT` with `WINEDBG=+seh`:

| | original | ours |
|---|---|---|
| alive after the request | yes | **no** |
| `ADDR_NAME_TABLE_COUNT` | 6 | 6 |
| `ADDR_LEVEL_TABLE_COUNT` | 10 | 10 |
| handshake checksums logged | six | none |

Both tables parse correctly -- 6 `RULES` and 10 `MAP` lines, exactly what
`mpmaps.txt` declares.

**The fault is `MpPanelUpdate` +0xa2**, `mov 0x58(%eax),%eax` with
`eax = rows[i]`, resolved with two anchors from that run's own patch lines.
`MP_PANEL_OFF_ARMY_ROWS` (+0x258) is READ every frame and written NOWHERE --
one use in the whole tree, and no `0x258` literal reaches the panel either.
The original does the identical pair of dereferences, so the update is not
what is wrong: **`MpPanelConstruct` never builds the four army-points rows.**

Not fixed in the diagnosing commit on purpose. The original fills that array
through a walking pointer rather than a `+0x258` literal, so the block has to
be read out of the constructor; writing it from the shape of the update would
be inventing a layout.

Two false trails died by measurement and are recorded so they are not re-run:
the `Lobby start` / `Releasing Comm Connection` pair before the death is
ORDINARY STARTUP and the original logs both, and the map-list block added to
`MpPanelConstruct` is cleared -- disabling it behind a switch leaves the exit
exactly where it was.

Still true and still worth acting on: this is the only configuration that
reaches the multiplayer widget tree, so every comm reconstruction verified "by
reading" has had no drive behind it for as long as this has been broken.

## What is next, as a number rather than a direction
## `ab.sh mission`'s `frames` figure is a MARKER COUNT, not a frame rate

A `mission` run of HudSquadUpdate failed the frame gate at 7452/379 -- the
original's side running away, as it always is, and ours below the gate's
floor of 500. All three exact oracles passed on the same run: state
identical, widget tree identical at 16 nodes, log identical at 13 messages.

What settled it was a probe rather than a re-run. On a live Boot Camp
mission our build composes **1,196 frames a second** under a load average of
8, and HudSquadUpdate runs 9,688 times against HudSquadPaint's 9,688 -- one
update per paint, exactly. So the reconstruction is neither slow nor broken.

The `frames` number counts the per-frame `-dbg` markers stripped from the
LOG, so it measures how much of the run was spent in live play, not how fast
the game went. Under load the drive spends longer getting through the two
dialogs and the marker count collapses while the frame rate does not. The
widget tree matching at 16 nodes is the proof our side did reach live play:
that artifact is taken after both dialogs are cleared.

Read this one as a drive-timing gauge. Its evidence is the log, the widgets
and the state, exactly as its own comment says the pixels are meaningless.

**BUT THE TWO MEASUREMENTS DO NOT RECONCILE, and that is worth stating rather
than leaving as a shrug.** The markers really are one per composed frame, and
both sides emit them across their whole log -- ours from raw line 56 of 653,
the original's from 484 of 26,170 -- so this is not our side reaching play
late. It is the original composing 26,170 frames where ours composes 653 in
the same wall-clock drive.

That cannot be squared with the 1,196 frames a second measured above. At that
rate a drive with any live play at all would show tens of thousands. Either
the marker is not emitted once per ComposeFrame on our side, or the 1,196
figure does not describe this configuration. **Both are measurements and one
of them is wrong**, which is a sharper open question than "the band went
stale".

What is NOT in doubt: it predates this session -- CLAUDE.md records 533, 652
and 625 across runs two of which are older than the work that first noticed it
-- and it is not a defect the other artifacts can see, since `mission`'s
state, its 16-node widget tree and its 13 log messages are identical every
time.

**THAT ANSWER WAS WRONG AND THE NEXT PROBE KILLED IT.** It read the recon
log's last line -- `calculating region data...` -- as the mission still
loading. Timed on both sides, RETURN to that message takes **2 s on the
original and 1 s on ours**, so the load is not slow and is not where either
side sits. A log that ENDS at a line means only that nothing was logged after
it; with the per-frame markers being the only thing that follows, it means no
frames were composed, which is a different claim and not the one made.

Both sides also finish showing the MAP -- their finishing screenshots carry
195 and 193 distinct colours with the same greens and browns -- so both reach
live play. The dialogs are cleared on both.

Driven by hand with the SAME clicks but more slack -- 46 s before BOOT CAMP
instead of `ab.sh`'s 20, then its own 25 and 30 -- our side reaches live play
and emits **33,494** markers in twenty seconds, comfortably past the
original's whole-run 25,797. So the build is not slow at composing; it is
slow at LOADING, and `ab.sh mission`'s fixed 30-second wait is enough for the
original and not for us.

That also disposes of the reconciliation puzzle above: the 1,196 frames a
second and the 608 markers were never measuring the same thing, because the
608 contains no live play at all.

**`ComposeFrame`'s counter is BLIND and cannot be used here**, which cost a
probe: it reads 0 in live play while the log takes 33,494 markers, because
its caller is reconstructed and reaches it directly. Count the markers.

**ANSWERED: THE MARKERS STOP WHILE THE GAME KEEPS DRAWING.** Our side does
not compose fewer frames; it stops EMITTING the per-frame `-dbg` marker and
goes on rendering. Measured in one drive:

- after both dialogs, 7,891 markers
- after six horizontal mouse-moves and 4 s, 8,638 -- already slowing
- after six VERTICAL mouse-moves and 6 s, 8,638 -- **delta zero**
- ten seconds idle, then scrolling back: still 8,638, so it never resumes

The game is fine at that point. The control socket answers `pong`, the pause
mask reads 0, `ctl pointer` is normal -- and scrolling again moves **202,859
pixels**, which is a frame composed and presented after the markers stopped.
So the `frames` figure stops being a frame count on our side partway through
this drive, and the gate compares two different things.

Ruled out along the way, each by a run rather than an argument: the initial
wait (28,386 against 28,249 at 20 s and 46 s), the PAUSE (both sides read
mask 0 -- now captured in the state artifact of every `mission` run), and
TRACE (26,333 against 633 with `AM2_AB_TRACE=0`, unchanged).

What is left is WHY the marker emission stops after a vertical scroll, in OUR
code since the original's does not stop. Worth having found: it is the same
log the A/B compares.

**THE LOG'S SHAPE IS IDENTICAL ON BOTH SIDES, which narrows it further.** Each
run is 13 header lines ending at `calculating region data...` and then nothing
but markers to the end -- 633 of them on ours, 26,333 on the original's. So
markers begin when the mission does, on both, and only the count differs.

**And the launch is not the difference.** `ab.sh` passes `extra=""` for this
configuration, so its command line is exactly the hand replication's.

**Run ORDER is not it either**, tested by hand: the original driven first
gives 20,212 markers and the reconstruction driven straight after it gives
**8,421** -- not the 633 the suite reports. So inheriting the previous side's
`Options.cfg` and running second do not reproduce it.

What that run DID confirm is the scroll: with the mouse-moves included our
side falls from ~28,000 to 8,421, which is the marker stop measured
independently.

So a hand replication of everything the suite does still lands an order of
magnitude above the suite itself, and the residue is unexplained. **This is
where the investigation stops being worth its cost**: five candidates have
been excluded by runs, the shortfall is in a LOG COUNT and not in the game,
and every other artifact this configuration produces is identical. Anyone
picking it up should start by instrumenting `ab.sh` itself -- printing the
marker count at each step of its own drive -- rather than replicating it a
sixth time by hand.

Worth stating plainly: this is not a defect in the game. Every other artifact
this configuration produces -- the state, the 16-node widget tree, the 13 log
messages -- is identical, the frame is demonstrably being composed, and the
gate is the only thing that disagrees.

Three explanations were offered and retracted before this one -- a stale band,
a still-loading map, and two irreconcilable measurements -- and each rested on
ONE artifact read in isolation. What settled it was changing one thing at a
time and a screenshot diff that asked whether the frame had moved.

## Driving to a live mission by hand needs ab.sh's WAITS

Three probe attempts read `HudSquadUpdate=0` and looked like dead code. The
drive was simply too impatient: `ab.sh` waits **25 seconds** after clicking
BOOT CAMP at (306,143) and **30 more** after `key RETURN tap`, and sends the
key by NAME rather than as scancode 28. With six-second waits the game sits
on the briefing with the load bar full and composes nothing.

`ComposeFrame` is the wrong liveness counter to check that with -- it is
BLIND, its caller being reconstructed, so it reads 0 in a healthy mission
and looks like confirmation that nothing is running. `HudSquadPaint` is an
honest one on this screen. CLAUDE.md already says to read a liveness counter
beside the one you care about; it does not help if the liveness counter is
itself blind, so check blindspots.py for the one you pick.

## COMPLETE, with the three blind spots each ruled out by measurement

tools/remaining.py reads **0 game functions and 0 C++ static initializers**
over every byte from 0x00401000 to the real CRT frontier at 0x00464420.

The number is only worth as much as the method, and the method was wrong
twice before it was right.  All three ways it could have been wrong are now
either fixed or ruled out:

| blind spot | how it was found | state |
|---|---|---|
| range: merged entries not split above the nominal CRT_START | six functions hidden, found by splitting the band by hand | fixed -- remaining.py splits the whole range |
| range: the jump-table and thunk classifiers testing against the nominal line | a 45-entry table counted as a 180-byte function | fixed -- both test against CRT_REAL |
| gaps: a function living where no entry covers | ruled out -- the entries tile .text from 0x00401000 to 0x00464420 with ZERO gap bytes | checked on every run |

The tiling check is the one that can be ruled out rather than fixed, and it
is the trig tables' rule applied to the function list: if a layout does not
tile, one of the bases is wrong.  remaining.py prints it every run, so the
claim stays verified rather than remembered.

## The fourth blind spot: "CRT by position" is an inference, and it holds

tools/crt.py labels 58 functions CRT from their own bodies and **171 more by
sitting above the evidenced frontier**.  That second group is an inference,
and if any of them were game code it would be untransposed work the counts
cannot see -- they stop at 0x00464420.

Two probes, and the sharper one is the second:

- Referencing game-range data does NOT distinguish them.  97 of the 230
  entries above the frontier touch 0x004F0000..0x00670000, because the CRT is
  statically linked and its own statics live in the same .data.  A test that
  cannot separate the two answers nothing.
- CALLING game code does.  Exactly TWO entries above the frontier call
  anything below 0x00462600, and both are explained: 0x004664C0 calls
  ADDR_WIN_MAIN, which is the CRT startup doing its job, and 0x0046D8D0 is an
  MSVC SEH unwind funclet -- `mov ecx,[ebp-0x50]; jmp WidgetDestruct`, sitting
  just past int3 padding -- which is compiler-generated and which this port
  does not reproduce by standing decision.

So nothing above the frontier is game code wearing a CRT label.  Worth
recording that the first probe was useless rather than only the second: a
test with no discriminating power reads exactly like a passing one.

## What is left, and why none of it is work

The section below claimed 0 game functions.  That was an artifact of HOW the
range was widened, not a fact about the work: `merges.real_functions()` stops
splitting merged entries at the NOMINAL CRT_START, so every entry in the band
above it was credited whole the moment one function inside it was
reconstructed.  Six functions were hidden that way.

This is the merged-entry error this file already documents twice -- once when
the naive count read 0, and once when matching on exact start hid WndProc --
recurring a third time, in the band that had just been added.  Widening a
range without widening the SPLITTING that goes with it produces a number that
gets better-looking as it gets more wrong.

tools/remaining.py splits the whole range now.  Read its output; the table
below is kept only for the description of what is not work.

## The three groups that are not work

Measured 2026-09-03. `tools/remaining.py` reads **0 game functions and 0 C++
static initializers**, over the full range to the REAL CRT frontier
(0x00464420, per tools/crt.py) rather than the nominal CRT_START -- which is
26 KB low and would have hidden 112 game functions.

What is still the image's, and why none of it is work:

| | entries | bytes | why |
|---|---:|---:|---|
| linker thunks | 18 | 288 | one `jmp` each into the real body; an incremental-linking artifact the original source never had |
| jump tables | 4 | 188 | data in `.text`, not code |
| harness / IAT | 4 | 4,246 | ADDR_LOG, which src/inject/gamelog.c owns, and the three `jmp [IAT]` import thunks |

The last group is the one worth being explicit about, because reconstructing
any of it is a DEFECT rather than progress. Replacing ADDR_LOG with an empty
function once silenced the game's log and blinded half of tools/ab.sh --
CLAUDE.md records the five configurations spent that way. And DirectInput
MUST go through its thunk, or our own import would resolve past
dinput_hook.c's patch, which tools/checkhooks.py exists to catch.

remaining.py classifies all three groups rather than counting them, and
prints "nothing left to transpose" when the two real counts reach zero.

## STALE: 82 functions, and 36 of them are one class

Measured 2026-09-03 at 1,580 patches, by the method below (split merged
entries through tools/merges.py, then containment WITHIN a real function):

**82 functions, 4,636 bytes** -- which reconciles exactly with the 84 /
4,732 measured earlier in the session: the difference is 2 functions and
96 bytes, the two 48-byte pad handlers transcribed since. Same method,
same answer, which is the first time two of these counts have agreed.

The decomposition is new and it matters more than the total:

| | functions | bytes |
|---|---:|---:|
| C++ static initializers | 36 | 896 |
| real game functions | 46 | 3,740 |

The 36 are the null-terminated function-pointer array at **0x00473004**
-- MSVC's `.CRT$XC` table, run by `_initterm` -- together with the 16-byte
incremental-linking thunks that jmp into them. They compute globals from
a .data geometry block: 0x00427640 is `[0x51307c] = 0x68`, the record size
that 0x004227DB then uses as a rep movsd count.

**They are reachable, which was not obvious and was nearly assumed away.**
Static initializers run from the CRT before WinMain, so the instinct is
that a patch on one can never fire. It does: tools/launcher.c creates the
process CREATE_SUSPENDED, calls LoadLibraryA remotely -- which runs
DllMain, which is where install() lives -- and only THEN ResumeThread. So
every patch is in place before the EXE entry point, hence before
_initterm. A patched initializer is called by the CRT and its counter
moves like anything else.

Two ways to get this wrong, both avoided by measuring rather than reading:

- A naive split reports **83 / 6,892** because it matches on exact start
  and so misses that ADDR_WND_PROC is 0x0040A6B0 filed under an entry
  beginning 0x0040A6A0 -- 2,256 bytes of reconstructed code counted as
  outstanding. Containment within the real function fixes it. This is the
  merged-entry error one step along, and it bit again while writing this.
- Roughly half the 16-byte "functions" in any raw list are pure `jmp`
  thunks with twelve nops after them. Nothing to transcribe. refs_to
  answers [] for all of them, which proves nothing -- it cannot see a
  call rel32, as CLAUDE.md says.

## STALE: it is 112 functions, not 31 -- and not 0

The "31 remain" table below is stale twice over. Most of its entries have
since been transcribed, and the method that produced it was wrong in a way
that gets MORE wrong as the work finishes.

Counting docs/functions.tsv entries below CRT_START and dropping any entry
that contains a patched address now answers **0 functions, 0 bytes** -- which
is the merged-entry false positive CLAUDE.md already documents for
coverage.py, arriving here by a different route. An entry that is several
functions is credited whole the moment ANY of them is patched, so the count
collapses to zero exactly when the last straggler in each merged entry is
still outstanding.

There is a concrete counterexample and it was found by accident, reading
SoldierNameOf's callers: 0x004158D0 is a thiscall HUD painter that nothing
has reconstructed, and it sits inside HudSquadDestruct's merged 2,800-byte
entry at 0x00415850. The naive count calls it done. It is also, at 2,568
bytes, the LARGEST thing left.

Splitting every merged entry through tools/merges.py first -- which is what
CLAUDE.md says to do before ranking anything -- gives the real figure:

**112 functions, 15,796 bytes.**

| bytes | address | name |
|---:|---|---|
| 2568 | 0x004158D0 | -- |
| 1200 | 0x00455340 | -- |
| 1184 | 0x00431E10 | ADDR_CHECK_MAP_RULES |
| 976 | 0x00421E80 | ADDR_EVT_CONDITION |
| 640 | 0x004171C0 | -- |
| 624 | 0x00411C20 | ADDR_COMM_FRAME_PRE_A |
| 544 | 0x00457E50 | -- |
| 512 | 0x00431A30 | -- |
| 464 | 0x0044CDA0 | -- |

and a long tail of DirectPlay callbacks, save-list handlers and menu widget
methods from 336 bytes down. Note how much of the tail is comm: those are
verifiable by reading only, on a machine that opens no session.

Do not hand-keep this table either -- recompute it. The one-line version is
merges.real_functions() to split, merges.reconstructed() to subtract.

## STALE: the earlier "31 remain" table, kept for the record

Reported for several turns, and it does not survive being measured. Taking
docs/functions.tsv below CRT_START, subtracting every patch_replace target
and the two REGISTERED reconstructions that are not patches, leaves **31
functions, 26,896 bytes**:

| bytes | address | name |
|---:|---|---|
| 3328 | 0x00416340 | ADDR_HUD_SQUAD_DETAIL |
| 2064 | 0x00414F20 | ADDR_SELECT_WEAPON |
| 1952 | 0x00418480 | ADDR_HUD_CHAT_SEND |
| 1472 | 0x0044D110 | -- |
| 1456 | 0x00453280 | ADDR_SAVE_LIST_CTOR |
| 1296 | 0x00431E10 | ADDR_CHECK_MAP_RULES |
| 1200 | 0x0042BEA0 | ADDR_LOAD_ATL_FILE |
| 1168 | 0x00425300 | ADDR_STATE2_ENTER |

plus 23 more from 1,024 bytes down to 192. Most of the tail is the menu
widget layer -- constructors and destructors for the save list, the game
menu, the overwrite and message dialogs, the multiplayer spinner, the three
HUD panels.

**Where the wrong number came from.** `docs/boundary.md` reports the
Win32/DirectX boundary, and that IS essentially finished -- 0 COM sites and
2 unreachable MessageBoxA sites outstanding. "The boundary is done" is true
and answers a narrower question than "every game function is
reconstructed". The two got conflated, and the second was then reported as
measured when nothing had measured it.

CLAUDE.md already warns about exactly this in another form: read the figures
from the generated doc rather than from prose, and know which question the
doc answers. The boundary doc's own header says "Only game code is counted"
and means only game code that touches the boundary.

**A patch count cannot detect this either.** 1,516 patches is more than the
1,239 entries below the line, because merged entries take several patches --
so the total going up says nothing about coverage of the function list.

Three of the four addresses above the line that look unpatched are NOT
functions and should never be counted: 0x0045CAA0 is the retail-stubbed
logger (a bare `c3`, patched by src/inject/gamelog.c, and reconstructing it
as an empty update once silenced the game log), and 0x00463390, 0x00463396
and 0x00464410 are one-instruction `jmp [IAT]` import thunks, one of which
must stay a thunk for dinput_hook.c's IAT patch to be reached.

## The standalone is drivable now, and it was accepting input into a hole

`make run PORT=1` launches the standalone and it REACHES THE MAIN MENU and
plays: title screen with every button painted, BOOT CAMP through to a live
mission with Sarge on the map, the HUD, the radar and the squad panel, and the
game clock ticking. The previous session recorded `PORT=1` as not working, on a
run that showed `control: disabled` and `DDERROR 80004001` in `InitDirectDraw`.
Nothing in the code was wrong: that is what the standalone does when it is
launched without a Wine desktop, which is already written down as the way to
run it. A launch error, read as a defect.

**What WAS wrong is that none of the injected input reached it.** The standalone
links `src/inject/input.c` and `src/inject/control.c`, so every `key`, `type`
and `mouse` command was accepted and acknowledged -- and dropped. Two things
were missing and each is invisible on its own:

- **Nothing called `input_init()`.** `dllmain.c` and `dinput_hook.c` call it and
  neither is in the standalone link.
- **Nothing applied the overlays.** In the injected build `dinput_hook.c` wraps
  the device's own `GetDeviceState` and `GetDeviceData` by patching the game's
  IAT. The standalone has no IAT to patch and no original device to wrap -- our
  `PollKeyboard` and `PollMouse` ARE the poller -- so the two overlays are
  applied there instead, at the points that hook applies them:
  `input_overlay_keyboard` onto the state just read and before `MirrorModifier`,
  so an injected shift mirrors the way a real one does; and `input_take_events`
  in `PollMouse`'s drain loop when the device has nothing left, so injected
  events come after real ones rather than masking them. `input_pump` runs once
  per poll, not once per event -- inside the loop it would retire a tap before
  the game had seen its press.

**The symptom read exactly like a dead port.** The menu painted, the cursor
moved when written (`cursor` writes globals, which needs no device at all), and
BOOT CAMP even sat highlighted -- so the screen said the widget layer was
alive. Only `ctl keys` distinguished it: nothing down while a key was held.
That is the command's whole purpose, and this file already says why -- `state`
is what the harness is injecting and `keys` is what the game sees, and a
channel broken in between shows up in one and not the other. First time the
distinction has been load-bearing.

`counts` says `(no counters: this build has no patch stubs)` here, correctly:
the counters ARE the patch stubs and the standalone has no patches. Do not read
that as a broken socket.

Measured after the fix, on a live Boot Camp mission, against the injected build
and the original doing the same drive:

    W held, x per second   standalone 85, 91, 92, 91
                           injected   86, 91, 92, 91, 92
                           original   92, 91, 91, 65, 91

    W and A held           standalone circle 20 wide, 17 tall
                           injected   circle 20 wide, 18 tall
                           original   circle 20 wide, 18 tall

so the standalone plays, and today's route-gate fix is in it.

## The whole suite after both of today's changes

`tools/ab.sh all`, 17 configurations, one run, after the route-gate fix and the
standalone input wiring:

| | evidence |
|---|---|
| bootcamp | state identical (1,610 objects), log identical, 22 pixels |
| windowed | log identical, 0 pixels |
| intro | log identical (its pixels are two unsynchronised playbacks) |
| audio | log identical, 22 pixels |
| mission | state, widgets and log identical; frame gate FAILS |
| combat | log identical; frame gate FAILS |
| campaign | widgets identical (35), log identical, 2 pixels |
| controls | widgets identical (26), log identical, 0 pixels |
| difficulty | widgets identical (7), log identical, 0 pixels |
| audiovol | widgets identical (14), log identical, 45 pixels |
| menuscreens | widgets identical (8), log identical, 0 pixels |
| movies | widgets identical (9), log identical, 0 pixels |
| multi | widgets identical (9), log identical, 0 pixels |
| mpoptions | state identical (5), widgets identical (131), log identical (35), 151 pixels |
| df | log identical (24), 0 pixels |
| state3 | state identical, log identical, 0 pixels |
| quit | log identical, 0 pixels |

So every artifact this suite compares is identical everywhere, and the only two
failures are the two frame gates -- `mission`'s, which this file already
documents as failing for reasons nobody has established, and `combat`'s, which
the previous commit measured as PRE-EXISTING and NARROWED by the movement fix.

**`combat`'s gate points the OTHER WAY from `mission`'s, on the same machine
and the same build, and that is not yet explained.** `mission` reads
26,369/8,461 -- the ORIGINAL running away, which this file calls structural and
attributes to the `AM2_NOPATCH` half having no trace stubs in front of it.
`combat` reads 16,505/44,385 and 16,592/43,432 on two runs, with OURS the
runaway. One build cannot be both slower and faster than the original for the
reason the trace stubs give, so at least one of those two configurations is
failing for a different reason than the file states.

Recorded rather than guessed at. What would settle it is what the `frames`
number actually counts -- the `-dbg` per-frame markers, which is not the same
question as how many frames were composed -- and nothing has checked that the
two sides emit one marker per frame in the same places.


## CLOSED: the Lock/Unlock bracket, and the queue that kept being wrong about it

A goal of its own, separate from the Win32/DirectX boundary: find the game's
software RASTERISERS by which functions call `LockSurface`/`UnlockSurface`.

- **The Lock/Unlock bracket batch is a different goal from the boundary, and
  its numbers were wrong.** It said "5 of 22 done" and named `DrawText` and
  `DrawSprite` among them; neither calls `LockSurface` or `UnlockSurface` at
  all. Measured: **29 functions** call the bracket and **29** are reconstructed
  — the bracket is COMPLETE, closed by HudSquadDetail, and the rasteriser
  goal this item tracks is finished
  — `RenderGlyph`, `RedrawMapRegion`, `CalibratePalette` and `DrawMenuCursor`,
  the last of which the old list predates, and the menu-widget painters that
  have landed since.

The queue for it was hand-kept beside a ratchet, and that is the part worth
keeping now the goal is closed -- it was wrong in both directions, repeatedly,
and each time the correction was a paragraph rather than a fix:

  **That shortlist listed functions that were already done**, which is what a
  hand-kept queue beside a ratchet always comes to: it named `0x0041CC40`,
  which is `DrawHLine` and had been reconstructed for some time, and
  `0x0041C7F0`, which is `DrawBlip3` and went in the same day this sentence
  was corrected. A count that only goes up cannot tell you a candidate has
  been taken; only re-reading the list against the patch list can.

  **And it did it again one entry later**, listing `0x0041C8A0`, `0x0041CA50`
  and `0x004149B0` as outstanding when all three -- `DrawBlipPulse`,
  `DrawBlipSquare` and `RadarBlipColour` -- had landed with the radar. The
  radar's five primitives are now all ours. So is `DrawSelection`
  (`0x00462120`, 688 B), the leader's caret and the selected units' health
  bars, which is the first of the batch that ordinary play actually reaches.

  Rather than keep writing the queue down, generate it -- the same argument
  that put the count in `tools/checkclaims.py`.

  **THE QUEUE IS EMPTY AND THE HAND-KEPT VERSION WAS WRONG IN BOTH
  DIRECTIONS.** It said "**four** functions and none of them small" and then
  listed TWO, `0x00462600` and `0x00416340` -- a count and a table that had
  stopped agreeing with each other, in a paragraph whose whole argument is
  that queues should be generated rather than written down. Both are
  reconstructed now, checkclaims reads (29, 29) for the bracket, and
  tools/remaining.py reads 0 game functions.

  So this entry is closed, and the way it failed is the argument for the
  advice above it: the list drifted from its own count before it drifted from
- **The Win32/DirectX boundary is DONE, and the reasoning is
  `docs/boundary-notes.md`.** Every Win32 call site in the image that can

The rule that came out of it is in CLAUDE.md and is the reason this section is
here rather than there: **generate the queue, do not write it down.** A count
that only goes up cannot tell you a candidate has been taken, and a list beside
a count drifts from the count before either drifts from the code.


## OPEN: what no drive here reaches, and what windowed mode looks like today

Moved out of CLAUDE.md, which is a policy file and had been carrying these as
though they were rules. They are state: each is true of this machine and this
suite today, and each would be closed by a drive nobody has written.

- Windowed mode runs and is worth using as a second configuration — the window
  is created, sized and positioned correctly (client area 640x480 at (4,30))
  and `CalibratePalette` fires. **It no longer stays black**: this entry used
  to say Wine hands back no lockable primary and the client area never draws,
  and that is not what happens here now — the area paints, mostly white, with
  a blinking 10×10 element in it. White rather than the title art is still not
  right, so fullscreen remains the configuration to verify against; what
  changed is that "it is black" can no longer be quoted as the reason a
  windowed comparison is trivially exact.
- Both DirectDraw `Restore` paths are untested. `LockSurface`'s is a real defect
  in the original — it publishes an uninitialised descriptor after a successful
  Restore without re-locking. Kept as-is deliberately; see `src/game/win32/surface.cpp`.
- **NO CONFIGURATION IN THE SUITE REACHES COMBAT, and that is one measurement
  rather than a dozen separate mysteries.** On a Boot Camp mission driven past
  both dialogs, with `ObjIsItem` climbing past 84,000 and the frame ticking,
  `ShotStrike`, `ApplyShotDamage`, `DamageObject`, `CreateMissile`,
  `FireWeaponAtPoint` and `FireWeaponAtObject` ALL read 0 -- and none of those
  counters is blind. Sarge stands there; nothing fires and nothing is hit.
  Pressing SPACE, both CTRLs, ALT and RETURN changes none of them.

  So every function whose only route is "something took damage" is cold for
  ONE reason, and the entries below that used to explain themselves
  individually -- `RemoveFromItemList`, `FreeItem`, `ShooterReact`,
  `ApplyObjFrame`'s DamageItem route -- are the same fact said four times.
  Getting any of them exercised needs a drive that makes a unit shoot, which
  this project does not have and which would be worth more than several more
  reconstructions: it would unlock the whole combat-consequence layer at once.

- **`RemoveFromItemList` is unexercised for a now-known reason.** Its gated
  caller is `FreeItem` (`0x004285F0`), which dispatches on the item kind and is
  the only route that unlinks. Neither runs on a campaign drive: 325 items are
  added during load and none is destroyed in the ~25 s observed, because
  nothing in that window shoots anything. Reaching either needs a mission
  driven long enough for something to die, which is a drive this project does
  not yet have -- not a missing code path.
- `CheckSaveTag` executes; it is reached by the save-file header read at
  `0x00425950` on any campaign start with a save present. The entry below
  predates that and is left for the others.

The durable half of all this is in `docs/combat.md` -- that no configuration
reaches combat is one measurement rather than a dozen mysteries -- and in
`docs/oracles.md`, which lists what checks those functions instead.
