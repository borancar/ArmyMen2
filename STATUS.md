# Status

A dated snapshot of where the reconstruction is, kept so a session does not
have to re-derive it. **`CLAUDE.md` and `docs/` are authoritative**; this file
is a summary and can be stale between updates. Every number below carries the
command that produces it, so it can be re-measured rather than believed.

Last updated: **2026-09-08**, frame-exact Lua injection over the step socket
(see TOOLING below).

## TOOLING (2026-09-08): frame-exact Lua injection

`tools/sidebyside.py` gained an **inject file** (`<out>/inject`): a line
written to it is sent to BOTH lockstepped games as an action for the SAME pump.
A `lua <code>` line runs through the new `am2_host_lua` hook in
`am2_replay_apply` -- on the game thread, at the pump boundary, before that
pump's frame is produced -- so a give/poke lands identically on both and they
stay frame-exact, replacing the earlier "skip the next N frames" hack (the skip
file remains for other transients). The dev runtimes install the hook after
`control_start()` (native `runtime.cpp`, hybrid `loader.cpp`); the retail build
links sdl.cpp but not the console, so the hook is null and the verb just logs.
The same chunk resolves `CheatLine` in either build: `sym("CheatLine")` native,
raw `0x00417B80` hybrid (`sym` returns 0 there; 0 is truthy in Lua, so test
`==0`). Injected lines are recorded to the run's `input.txt`, so replays
reproduce the give. See docs/lua.md.

## OPEN DIVERGENCES, newest first

The rule (2026-09-07): `tools/sidebyside.py` runs EXACT -- no tolerance,
no forgiven pixel class -- and the first differing frame is the issue.
Newest first: a divergence found while fixing another is fixed before
returning to it. Each entry names its reproduction; an entry moves to
FIXED below when that replay runs identical.

- **FIXED (2026-09-10): a bazooka missile's terrain-crossing height clamp was
  inverted, so it detonated a pixel off.** Found live (trap pump 1772, frame
  1905, 143 px in box 50,197-65,212 -- the explosion; repro
  sessions/bazookamissile-1772.txt). The missile (type 5, 0x58f) flew identically
  to pump 1771; at 1772 the hybrid clamped MISSILE_OFF_HEIGHT (0x42) from 28 to
  24 as it crossed from open onto blocked terrain, while the port left it 28, so
  it detonated at x=1428 vs 1427 and spawned a different explosion set. StepType5
  (item.cpp) clamps the height when crossing tiles: the original adjusts when
  HEIGHT > want (`cmp; jle` past the block at 0x0043C54A; want = ground +
  overhead), clamping the flight DOWN; the reconstruction had `if (HEIGHT <
  want)`, the opposite sense, so it never lowered a too-high missile. Fixed the
  comparison to `>`. The recording then replays frame-exact past 1772 (55,447+
  frames, no differing frame). RNG/heap identical, so it was a pure StepType5
  transcription bug.
- **PARTLY FIXED (2026-09-10): a FROM-SAVE load desynced the heap by running
  BuildMapObjects on the load** -- the port's LoadMap (map.cpp) called
  BuildMapObjects unconditionally, but the original skips it on a load (after
  BuildRespawnPool at 0x0042D0B2 it tests ADDR_LOAD_PENDING and `jne`s past the
  build loop to the free-temp tail 0x0042D325), because the objects come from
  the save (LoadItems). Rebuilding the map's objects on every load was ~1250
  extra alloc/free ops that shifted every fixed-heap address. Fixed by guarding
  the call `if (ADDR_LOAD_PENDING == 0)`, matching the camera guard just above
  it. Verified: sessions/roachheap-1575.txt now has the heap ALIGNED (Sarge and
  the roach at identical addresses on both builds), and a fresh-start replay
  (tests/replays/bootcamp.txt) stays frame-exact -- so BuildMapObjects still
  runs on a fresh start. What REMAINS at that trap is the roach heading
  divergence itself, now proven INDEPENDENT of the heap (roach 0x400003ea still
  pos 2452,1174 vs 2451,1169 with the heap aligned): it is the SAME sight-scan
  bug as the 8583 entry below, so the fixture still traps at 1575 until that is
  fixed.
- **OPEN (2026-09-10): a FROM-MENU session desyncs the heap at mission load
  (~pump 490), and a roaming roach then diverges downstream.** [SUPERSEDED by
  the PARTLY FIXED entry above -- the heap desync was BuildMapObjects on load;
  the roach divergence is the 8583 sight bug, independent of the heap.] Found live
  (fresh `--video`, no replay; trap pump 1575, frame 1703; repro
  sessions/roachheap-1575.txt). At the trap only roach 0x400003ea (type 8,
  FULL health -- not hit) differs, pos (2452,1174) vs (2451,1169), and its
  FIELD_540/facing diverge with RNG IDENTICAL -- the same heading mechanism as
  the 8583 entry. BUT the cause here is different: EVERY object is at a
  different heap address between builds (Sarge 0x3e8 at 0c650bdc vs 0c1d45ec),
  so the heap is globally desynced and the roach's sight-scan iteration/tiebreak
  order shifts. AM2_TRACE_HEAP diff (both builds, replay of the recording):
  identical through op 1058, then at PUMP 490 the port makes ~1254 MORE
  allocations (and ~1242 more frees) than the hybrid -- ~1250 extra alloc/free
  pairs during the sprite/object load, spread across CreateItem (192B, item.cpp
  :12249), BuildRowsFromDef (96B, maprow.cpp:1099), MakeRecordList
  (objtype.cpp:264), LoadMaskPacked (misc.cpp:532), PreloadSprite/
  SpriteLoadFromDataFile (64B, sprite.cpp) and ObjInitCommon. The named sprite
  loads MATCH in count (alloc 3517 x1 both), so it is not an extra sprite --
  it is a per-object/row temporary the reconstruction allocates-and-frees where
  the original does not, which reorders the allocator's free list and shifts
  every later address. The 8583 sight divergence did NOT show this because that
  session began from a replay recorded AFTER the load, already aligned.
  ROOTED (AM2_TRACE_HEAP op-sequence diff, sites via addr2line): both builds are
  aligned through op 1058 (`alloc 268`, BuildRespawnPool). Then they FORK on the
  order of the SPRITE-SET RELOAD versus BuildMapObjects, and this is a from-SAVE
  load so it is in the save-load chain Boran flagged (docs/saveload.md). The
  HYBRID, right after BuildRespawnPool, does the reload as a BLOCK -- 16x
  free(NULL) over the sprite-set array (FreeSpriteSets, winmain.cpp:410) then a
  batch of GrowSpriteList/LoadSpriteSet -- and THEN creates the map objects. The
  PORT runs BuildMapObjects FIRST (op 1059+: PreloadSprite, SpriteLoadFromDataFile,
  CreateItem, ObjInitCommon, BuildRowsFromDef, RowAlloc, per object) and does the
  same free-16 + LoadSpriteSet reload block ~1600 allocations later (op 2674).
  So the sprite buffers and the object/row blocks are allocated in OPPOSITE order
  and every later address shifts. free(NULL) is a no-op and does not shift the
  heap; the real reorder is the LoadSpriteSet batch vs the CreateItem/RowAlloc
  run. Fix: reorder the port's save-load sequence so the sprite-set reload runs
  before BuildMapObjects, matching the original -- but it sits beside the
  deliberate LoadItems/ItemsReset deviation and the original's latent cell-grid
  use-after-free, so place it by reading State2Enter's load chain against the
  disasm rather than by moving a call blindly. Repro sessions/roachheap-1575.txt.
- **OPEN (2026-09-10): a hit roach's heading diverges -- the port's roach
  SightScan finds an observer the hybrid's does not, so it turns toward it.**
  Found live under tools/sidebyside.py after firing a bazooka at a roach (trap
  pump 8583, frame 8414; repro sessions/bazookaroach-8583.txt, replayed with
  --tolerance 12 to pass the magnifying-glass 4-px residual first). At the trap
  only roach 0x400003ec (type 8, army 2) differs: OBJ_OFF_FACING 23 vs 41,
  OBJ_OFF_FIELD_540 (the target heading) 237 vs 68, subpixels, and pos.y by 1 --
  all downstream of FIELD_540. RNG seed (0x2f0c6a4a), game clock and
  ADDR_SIGHT_GENERATION (0x0e6c) are IDENTICAL, so it is deterministic, not an
  RNG desync. NARROWED (gdb watchpoint on FIELD_540): RoachBehaviour
  (region.cpp:9061) routes the roach to heading 237 on both, then its tail
  `CopyByteIfSet(obj, &FIELD_540, ctx)` (region.cpp:9116, misc.cpp:61) overrides
  it with `ctx[SIGHT_OFF_BEARING=0x18]` IFF `ctx[SIGHT_OFF_OBSERVER=0x10] != 0`.
  On the port ctx.observer is set (roach sees a threat -> F540<-68); on the
  hybrid it is 0 (sees nothing -> keeps 237). So the port's per-frame roach
  SightScan (RoachBuildContext -> 0x00403B40) finds an observer the hybrid does
  not. Same subsystem as the pump-2464 sight bug, a different instance and the
  opposite direction (there the port UNDER-saw; here it OVER-sees). Next: catch
  the roach's SightScan on both -- which object it accepts as the observer, and
  why AiCanSee / the directional cache answers differently for the roach's
  bearing -- the method that fixed 2464.
- **OPEN (2026-09-10): the magnifying glass's aim marker has a 4-px 2x2
  refraction residual (box 476,32-477,33) after the aim-cursor fix below.**
  With UNIT_OFF_FIRE_X now identical, `DrawEffectLayer` (0x004123D0,
  mapdraw.cpp:2421) draws the marker in the right place, but its 112x112
  refraction block -- which reads displaced offscreen pixels through
  `g_framebuffer[sy*g_pitch + sx]` (ADDR_AIM_DISPLACE_MAP at 0x00478CDC, in
  .data and carried) and writes them back -- differs at one 2x2 cell: hybrid
  (66,69,66) vs port (123,121,123). It is the ONLY differing block in the
  frame. DrawEffectLayer is cold (no drive reached a pointer-mode aim weapon
  before this), so its refraction has never been A/B'd; the 4 px are a
  distinct issue from the .text-gap read fixed below, only now reachable.
  Repro sessions/magnifyingglass-7655.txt (traps at pump 7655, now 4 px).
  Next: catch the refraction mid-draw (g_pitch and the source sx,sy are 0
  post-frame, set only under Lock) -- break in DrawEffectLayer's read loop on
  both builds and compare the offscreen source pixel and g_pitch for the 2x2
  cell.
- **OPEN (2026-09-10): a roach's BITE-state transition diverges -- hybrid bites
  Sarge, port still approaches.** Live repro sessions/roachbite-1112.txt (trap
  pump 1112, frame 1238, 1274 px box 218,193-312,268). Object tables, RNG and
  all positions identical; roach 0x400003ef's OBJ_OFF_FIELD_530 is 4 (biting) on
  the hybrid and 2 (approaching) on the port, with OBJ_OFF_DEADLINE_58 and the
  bite fields (0x54c-0x55d, 0xc4) set only on the hybrid -- so the sprite is the
  bite animation vs the walk. FOURTH manifestation of one recurring roach bug,
  all in RoachBehaviour/RoachStepTailA and all deterministic from identical
  state: 8583 and 1575 diverge on FIELD_540 (heading / which observer it faces),
  1391 on FIELD_44 (speed 0, move refused), 1112 on FIELD_530 (arrived->bite vs
  approach). Common root is the roach's sight/distance decision in RoachBehaviour
  answering differently from byte-identical inputs -- so the fix is one dig into
  RoachBuildContext->SightScan (0x00403B40) / the AiCanSee cache the roach reads,
  not four separate ones. Recommend chasing it as a dedicated RoachBehaviour
  investigation rather than per-manifestation.
- **OPEN (2026-09-10): after the RoachBehaviour dropped-tail FIX, a roach's
  SightScan bearing still diverges from byte-identical state.** With the tail
  restored the roach now bites on both builds, and roachbite-1112 advances to a
  trap at 1116. NARROWED: the roach is byte-identical through pump 1114
  (position, subpixel, facing, FIELD_540, state 4, deadline) and at 1115 its
  FIELD_540 diverges 63(hyb)/95(prt) -- CopyByteIfSet writes FIELD_540 =
  ctx[SIGHT_OFF_BEARING], and that bearing is 100 vs 95 despite identical roach
  and Sarge positions. So the roach's SightScan (RoachBuildContext ->
  0x00403B40) answers a different bearing/observer from identical inputs -- the
  same directional-sight subsystem as the pump-2464 bug (AddSightBlocker /
  ADDR_SIGHT_BLOCK_BY_DIR / AiCanSee), a residual instance the target-acquisition
  fix now exposes. Repro sessions/roachbite-1112.txt (traps 1116). Next: catch
  the roach's SightScan on both at 1115, compare what it finds and the
  directional cache it reads for the bearing. This SUPERSEDES the earlier
  "move-refusal" framing: the move refusal (speed 0) was downstream of this
  heading divergence, not a separate RoachMaskWeight bug.
- **SUPERSEDED (see above): a roach attacking Sarge diverges by 1 px -- the port
  REFUSES a move step the original takes.** Live repro sessions/roachattack-1391.txt
  (trap pump 1391, frame 1519, 810 px box 212,199-303,268). Heap aligned, RNG
  identical; only roach 0x400003ef differs, pos (2108,762) vs (2109,761), and
  its OBJ_OFF_FIELD_44 (speed) is 130 on the hybrid but 0 on the port -- same
  facing (63), same OBJ_OFF_FIELD_540 (63), same subpixel and pos through pump
  1390. So this is NOT the heading/CopyByteIfSet path of the 8583 entry; it is
  the SPEED path. RoachStepTailA (0x0043D750, item.cpp) refuses the step when
  `RoachMaskWeight(obj, next, landing, 1) >= RoachMaskWeight(obj, cur, pos, 0)`
  (the landing is no better than here) -> speed 0. The port refuses at 1391, the
  hybrid does not. Since pos/facing/subpixel match going in, the divergence is
  in RoachMaskWeight (cell weights / footprint mask) or in the ctrl record
  RoachBehaviour hands RoachStepTailA (ROACHSTEP_OFF_STATE/FACING/FLAG18). Next:
  catch RoachStepTailA on both at 1391 and compare `here`, `next`, `at` and the
  two RoachMaskWeight results -- if those inputs match, the weight table
  diverged; if not, it is the RoachBehaviour ctrl (same subsystem as 8583).
  MAY share a root with the 8583/1575 roach entries.
- **OPEN (2026-09-08): pause menu, SECOND open with the cursor already resting
  on a button -- one-pump hover-focus lag, ~1336 px over the button column
  (box 267,138-376,268).** Found hand-playing under `tools/sidebyside.py`
  (trap pump 11034, frame 10850; repro `sessions/pausefocus-11034.txt`
  = the run's recorded input.txt). At the trap: same dialog (308328080), same firstChild
  (308327952), same cursor (289,265, inside the ABORT rect 245,250,397,275) --
  the ONLY difference is `focusedChild` (dialog +0x34): the hybrid keeps it on
  the construct default (RETURN, the first child) while the port has moved it to
  the hovered button (ABORT). The highlight sprite follows `focusedChild`
  (ButtonPaint), so ABORT lights on the port and RETURN on the hybrid.

  Mechanism: `ButtonUpdate` (device.cpp path) takes hover-focus with
  `if (orig_mouse_moved) FOCUS(w,1)` when the cursor is in its rect -- faithful.
  So the port's `orig_mouse_moved` (ADDR_MOUSE_MOVED 0x00485494) was 1 at the
  open pump where the hybrid's was 0: the cursor was STATIONARY over ABORT on
  the 2nd open, so the original leaves focus at the default, but the port stole
  it. NOT persistent -- resuming and letting the cursor move (317,255) took BOTH
  to focusedChild=308334384 (ABORT), so the frames re-converge; it is a
  one-pump LEAD of the port's mouse-moved flag at the pause-menu open. The two
  run DIFFERENT `PollMouse` (port = our device.cpp reconstruction with the
  `input_take_events`/`input_pump` path; hybrid = the original), both reading
  the platform DI buffer, so the suspect is a one-pump timing difference in when
  `g_mouseMoved` is set/cleared relative to the dialog-open pump (ESC-release
  edge) between the two.

  To root-cause (needs a headless replay, so do it when no live run is up, to
  avoid rebuilding under one): replay sessions/pausefocus-11034.txt under
  `tools/lockstep.sh` with a per-pump trace of `g_mouseMoved`, the dialog
  pointer at 0x0065A058 and its +0x34, and the ESC key edge, on both builds;
  find the first pump where the port sets mouseMoved and the hybrid does not.

- **OPEN (2026-09-08): the flamethrower's flame trail renders one animation
  frame off while firing, ~80 px, box near the plume.** Found hand-playing the
  flamethrower under `tools/sidebyside.py`; trapped twice (pumps 3587, 3014).
  Only ONE object differs -- the newest def-3 flame-trail segment -- and only
  its VZ: port -64.0, hybrid 0.0, which makes the port's segment FALL (height
  28->27), shifting its sprite rect a pixel. Every end-of-pump field on Sarge,
  his SIGHTCOUT, and the object tables are byte-identical.

  ROOT-CAUSED (2026-09-08) to a SUB-PUMP ORDERING of a height-settling value,
  via `sessions/flamevz-32208.txt` replayed step-locked under sidebyside (the
  headless file-replay under lockstep.sh does NOT reproduce it -- see the note
  below). `CreateMissile`'s a6 = `spot.ground` (item.cpp:13156) and it computes
  `VZ = (spot.ground - height) << 1` with height=28. TrooperFire's no-target
  arm (the flamethrower's) sets `spot.ground = *(int16 *)(SIGHTCOUT + 0x18)` =
  `obj + 0x594`. That one address is BOTH `SIGHTCOUT_OFF_YADJ`
  (SIGHT_OUT_T2 0x57C + 0x18) and `UNIT_OFF_FIRE_Z` -- the sight-resolve
  (`ConsiderSightingC`, which writes the OBSERVED object's `OBJ_OFF_ROW0_Y_ADJUST`
  there) and the fire share it. Traced with a FLAMEZ line (region.cpp
  TrooperFire + a hybrid TrooperFire trampoline, both now reverted): on the
  PORT every fire read `ground=28` EXCEPT the last (the trap pump), which read
  `-4`; `ground == sightYadj` on the port on every fire, so it is always the
  no-target arm. At the trap BOTH read `-4` (targetUid=0, hit=0, fireZ=-4,
  fireX=3670 -- byte-identical), so obj+0x594 transitions 28 -> -4 DURING pump
  32208 and the port's TrooperFire reads it AFTER the write while the hybrid
  reads it BEFORE: port a6=-4 (VZ=-64), hybrid a6=28 (VZ=0). So it is not a
  read-site or branch bug -- it is the observed object's row0y settling by one
  pump, the same CLASS as the flame /200 and item-height divergences, feeding
  the aim-ground read.

  On aligning traces: `ADDR_GAME_CLOCK_MS` IS identical on both builds when
  they are genuinely in sync -- step-locked under sidebyside it read 44032 at
  pump 3000 and 159982 at pump 10000 on BOTH. (An earlier note here claimed it
  diverged even step-locked; that was a mis-sample -- the FLAMEZ clocks that
  looked different, 157k vs 176k, were fire events from LATE in the run, after
  the pause-focus divergence at 11034 had been skipped past, so the two builds
  had been allowed to drift. Not same-pump readings.) The clock resets to 0 at
  level load and only accumulates during gameplay frames, so clk/pump < the
  16.67 ms pump step is expected. Still align traces by PUMP, since clk resets
  and, once any divergence is tolerated, drifts.

  NEXT: find which object's `OBJ_OFF_ROW0_Y_ADJUST` transitions 28 -> -4 at
  pump 32208 (the observer the sight cone picks up) and why the two builds
  settle it a pump apart -- then the fix is the same shape as the other
  height-settling fixes. Repro: replay `sessions/flamevz-32208.txt` step-locked
  under `tools/sidebyside.py --replay` (skip past the pause-focus trap at 11034
  with `echo 150 > <out>/skip`); it traps at the flame at pump 32208. Give the
  flamethrower live with `"village people"` (case 12, keeps the rifle) or
  `phoenix!` (case 9, swaps it out but is equipped and fires at once).

FIXED by this rule so far, each with the replay that reproduces it:
- **FIXED (2026-09-10): RoachBehaviour dropped two statements of its shared
  tail, so a roach never kept a target -- the root behind all four roach
  divergences (8583/1575 heading, 1391 speed, 1112 bite).** The original tail
  (0x00408A27..0x00408A45) runs three things on every path; only the first was
  reconstructed. Added the other two: (2) `if (ctx.OBSERVER) OBJ_OFF_FOLLOW_UID
  = OBSERVER->uid` -- persist the sighted target so it re-acquires next frame
  (0x00408A3C); (3) `ConsiderSightingB(obj, out, ctx)` -- commit the sighting,
  which is what writes the BITE state FIELD_530=4 (0x00408A45). Without them the
  port's roach always had SIGHT_OFF_LEADER 0 in its ctx (took RoachBehaviour's
  no-leader branch) and never bit. Verified: the port now acquires FOLLOW_UID
  0x3e8 at pump 1000 like the hybrid; make check green; tests/replays/bootcamp
  frame-exact (no regression to fresh starts). Fixtures sessions/roachbite-1112,
  roachattack-1391, roachheap-1575, roachbite-... reproduce the family. A
  SEPARATE residual remains (see the roach move-refusal OPEN below): with the
  target now kept, roachbite advances 1112->1116 where the roach's move step
  still diverges by ~2 px -- the RoachStepTailA / RoachMaskWeight speed path of
  the 1391 entry, next to chase.
THE SQUAD PANEL'S NAME GUARD WAS INVERTED, so a green trooper carrying a custom
scripted name (e.g. "wildblood") showed a RANDOM soldier-table name in the HUD
SQUAD detail instead of the scripted one -- the original drew "Wildblood", the
port "D. DuBois" (found live under tools/sidebyside.py, trap pump 4479 / frame
4397, 161 green pixels in the HUD box 556,275-603,282; repro
scratchpad/live-4479.txt). HudSquadDetail (0x00416340) tries the scripted name
from ADDR_SCRIPT_NAMES first: at 0x004166FE it does `strncmp(name,"green",5)`
and `je` to the SoldierNameOf path -- so it uses the scripted name when it does
NOT begin "green" (a "green..." id is internal and is skipped for a personal
name). The reconstruction (widget.cpp HudSquadDetail) had the sense inverted
(`== 0`), so it used the scripted name ONLY when it began "green" and hid every
custom name behind a random one from ADDR_SOLDIER_NAMES. Everything readable was
identical -- selection (unit 0x3ea on both), cursor, hover, view origin, RNG
seed, the soldier-name "taken" table (empty on both), and the scripted-name
index itself (obj+0x0C = 26 -> "wildblood" on both) -- which is what pointed at
the DISPLAY guard rather than any state: same inputs, different rendered name.
Fixed by inverting the guard to `!= 0`. The recording then replays frame-exact
past 4479 (50,980+ frames, no differing frame). Invisible to every A/B: the
squad detail is only drawn when a unit is selected in a live mission, which no
scripted drive does, and the object tables it dumps do not include the rendered
name.
ADDSIGHTBLOCKER DROPPED THE ONE-TILE DISTANCE PAD IN THE BAND WRITE, so every
directional sight-cache band the port wrote was 16 units (one tile) short, and a
green trooper's `AiCanSee` therefore read out-of-sight where the original read
in-sight -- the pump-2464 green-trooper divergence (repro the combat run's
recorded input.txt, `/tmp/sbs/input.txt`; trap was frame 2158 on 0x3ee, a type-2
army-0 aimode-1 trooper whose AiGuardStep set OBJ_OFF_FIELD_C0 on the port and 0
on the original). The original `AddSightBlocker` (0x004036F0) computes the
silhouette distance `dist = max(ApproxDistXY(near), ApproxDistXY(far))` and then
does `add esi,0x10` ONCE (at 0x004038AD), so both the sight-range check AND the
LOW/MID/HIGH band stores read `dist + 16` (AM2_SIGHT_DIST_PAD). The
reconstruction (air.cpp) added the pad only in the range check and wrote the
UNPADDED `dist` into the bands, leaving every band exactly one tile short. NAMED
BY A GDB CAPTURE ON THE ALIGNED HEAP: at gen 565 both builds' viewer roach
(0x400003ec) and blocker (0x800004fb) were byte-identical (box, pos, facing 70,
heights 32/32) yet the port wrote 167 to record 23's HIGH where the hybrid wrote
183 -- exactly 16 apart at every diverging record and pump (167/183, 148/164,
208/224), which a geometry difference could not produce but a missing constant
does. The hybrid's AddSightBlocker frame at the record-23 store showed
`esi=0xb7=183` right after `add esi,0x10`. Fixed by folding the pad into `dist`
once, so the range check and the bands both read the padded value, matching the
single `add esi,0x10`. The combat replay then runs frame-exact from 2464 to the
END of the recording (94,139 frames, no differing frame; sight cache
byte-identical at gen 565 after the fix). Invisible to every A/B and every prior
replay -- no drive reached a sighting through a moving blocker in combat, which
only became reachable once the AI-dispatch fix below carried the replay past
1460.
A TROOPER'S AIMODE-6 DISPATCH RAN SARGE'S ARM, so an enemy soldier that should
attack (kneel and fire) never dropped into the firing pose -- the "missing
missile" (trap pump 1304 on 0x3ef, then 1460 on 0x3ed once the heap was aligned;
repro the combat run's recorded input.txt). The original has TWO AI dispatchers,
SargeAiStep (0x00407020) and TrooperAiStep (0x004062B0), each with its own
AI_MODE jump table (0x0040716C and 0x0040643C). Diffed byte for byte the two
tables agree on every arm but AI_MODE 6: Sarge's calls AiEngageStep (0x00406B30),
a trooper's goes through the thunk 0x00405D10 to AiPatrolStep (0x004057D0). The
reconstruction had merged both into one AiStepDispatch that always took
AiEngageStep. AiPatrolStep copies OBJ_OFF_SCRIPT_STATE (0xb4) into
OBJ_OFF_SCRIPT_ID (0xb0) -- the AI saved-point -- at its head and carries its own
engage path; AiEngageStep never touches 0xb0. So an aimode-6 enemy trooper lost
that save at spawn and then made a different pose/facing decision. NAMED BY A GDB
WATCHPOINT, which the aligned heap made possible: the object's address is now
stable, so `watch *(unsigned int*)<obj+0xb0>` in the hybrid stopped on the write
at 0x004057F1, inside AiPatrolStep, reached from TrooperAiStep's AI_MODE-6 arm --
which the reconstruction was not calling. Fixed by giving AiStepDispatch a `sarge`
flag: case 6 is AiEngageStep for Sarge and AiPatrolStep for a trooper, matching
the two original tables. The combat replay then runs frame-exact from 1460 to a
new trap at 2464 (a separate green-trooper divergence, now OPEN above). The whole
chase is the deterministic-paradox pattern taken to its end: identical object
tables, identical heap (after the teardown fixes), identical RNG and clock, and
the single differing byte (0xb0) named its writer once a watchpoint could reach
it. Invisible to every A/B and to the earlier replays -- no drive reaches an
aimode-6 enemy trooper.
THE MISSION-SECTION TEARDOWN LEFT THE FIXED HEAP MISALIGNED, because three
reconstructed teardown functions dropped frees, so every allocation after the
teardown landed at a shifted address. Found hand-playing Boot Camp's combat
section under `tools/sidebyside.py` and diffing `AM2_TRACE_HEAP` logs: the two
games' heaps were byte-identical up to the section change (pump 398/580 in the
recordings), then the port ran fewer/reordered frees. Three drops, each a
disassembly-confirmed omission: `SeqCtxFree` (0x00460E30) freed each record's
row but not the records ARRAY itself, and skipped the five header clears and
the per-record NEXT/PREV = -1 resets; `FreeMapLayers` (0x0042D3D0) dropped the
`TILE_REVEAL_GRIDS[4]` free loop between TILE_FLAGS and TILE_COVER; and the same
function dropped its whole TAIL (0x0042D550..0x0042D571) -- `ItemsReset`, both
`MapDescFree`s, `FreeAaiTables`, `FreeSpriteRegistry` -- which deferred those
frees to the next load and so ran them in a different ORDER, the point the trace
showed the port in FreeAnimTable where the original was still inside ItemsReset.
All five tail functions are idempotent (each guards its pointer and clears it),
so running them at teardown as well as at load is safe. With the three fixed the
heap is byte-identical across the builds through the whole combat sequence
(every object at pump 1459 sits at the SAME address, where before the delta was
~0x1470), and `make check` plus five committed replays (board, halftrack,
jeepfire, mineplace, rallyfollow) run frame-exact. NOTE: this did NOT clear the
combat trap it was found chasing -- see the OPEN combat entry; with the heap
aligned that divergence is isolated to a trooper's OBJ_OFF_SCRIPT_ID (0xb0)
dropped at spawn, which is a separate AI bug rather than a pointer tie-break.
A FOLLOWER VEHICLE'S FACING DRIFTED because Step3ChooseFacing never reset its
AI facing-search counter on the give-up exits (trap pump 6591, frame 5934;
repro `sessions/vehmove2-6591.txt`; hand-played ordering multiple vehicles to
move, exposed only once the formation fix above let a follower actually reach
its slot and cluster). Every comparable input was byte-identical -- the full
object (0x00..0x600), its row structure, the RNG seed, the view/cursor globals,
the frame delta -- yet OBJ_OFF_FIELD_574 (`o+0x574`, a PERSISTENT search
counter) read 3 on the port and 0 on the original, which fed Step3NextCandidate
a different facing and cascaded to a 4,495-pixel divergence. A hybrid
trampoline over VehicleBlockWeight showed the block weights and the CALL COUNT
identical on both -- same search work -- so the port was not searching more, it
was failing to RESET. Step3ChooseFacing (0x0045C8D0) has one reset block at
0x0045CA85 (`mov [esi+0x570],ebx; mov [esi+0x574],ebx` with ebx==0) that FOUR
exits jump to: the three early returns (MP, player-driven, record-satisfied)
and the past-the-interval arm's "current heading is clear" exit (0x0045CA05).
The reconstruction had written all four as a plain `return`, so 0x574
accumulated across frames on the port where the original zeroed it. Fixed by
resetting 0x570 and 0x574 to 0 on those four exits; the short-interval arm's
clear exit and the search-loop exits return WITHOUT the reset, as the image
has them. Replay then runs identical (no trap past frame 5934). Invisible to
every A/B: both drivable missions start with a squad of one, so nothing places
a follower to cluster. (Found the hard way with rebuild+trampoline traces; a
gdb watchpoint on 0x574 in the hybrid would have named 0x0045CA85 in one step.)
A FOLLOWER VEHICLE ORDERED INTO A GROUP MOVE STOOD ON ITS LEADER instead of in
its formation slot (trap pump 11373, frame 11243; repro
`sessions/vehmove-11373.txt`; hand-played selecting two vehicles and ordering
them to a point). Everything was byte-identical through pump 11350; on the
order pump the follower 0x3f4 was attached to leader 0x3f6 (FOLLOW_UID and
aimode 3 set correctly on both), but the original wrote its destination
OBJ_OFF_FIELD_C0 to a FORMATION point (1227,2707), 128 west of the leader,
while the port left C0 at the leader's raw position -- so the port read as
arrived (out state 1) where the original kept routing to the slot (out state
4). AiBuildContext (0x00407D70), the vehicle AI context builder, had been
reconstructed to copy the leader's raw position into SIGHT_OFF_DEST
UNCONDITIONALLY, on a note that only its twin TrooperBuildContext resolves a
formation point. The disassembly at 0x00407DE6 shows otherwise: it resolves a
formation point too -- `ResolveFormationPoint(obj, leader, &DEST)` when the
leader is an allied type 2/3/8, the raw position only otherwise -- and the one
real difference from the twin is ObjsAreAllied's third argument, 0 here against
the twin's 1. The follower's mode-3 step AiStepFollow copies SIGHT_OFF_DEST
into C0, so the dropped branch put the slot at the leader's feet. Fixed by
giving AiBuildContext the same conditional the twin has. Found by a live
side-by-side trace of the original's ResolveFormationPoint: on the order pump
it fired once -- foll=0x3f4 lead=0x3f6 out=1227,2707 -- from caller 0x407e11 =
AiBuildContext+0xA1, a call the port did not make. Replay then runs identical
(no trap past 11243). Invisible to every A/B: both drivable missions start with
a squad of ONE, so no scripted drive ever places a follower (tools/blindspots
and formationcheck already say the whole follow layer is cold here).
A NON-SARGE UNIT ORDERED ONTO A VEHICLE BOARDED ONE PUMP LATE on the port (trap
pump 3355, frame 3197; repro `sessions/board-3355.txt`; hand-played sending a
second soldier to a jeep the first had already entered). Every measurable input
was byte-identical the pump before the board -- the soldier's 1536-byte record,
its row structure, the RNG seed, the game clock and the frame delta all agreed
on both sides at pump 3354 -- yet the original boarded at pump 3355 and the port
did not until 3356. That ruled out drifting state and pointed at a missing code
path. A live side-by-side trace of the ORIGINAL's `EnterVehicle` caller (a
trampoline detour in the hybrid, logging `__builtin_return_address(0)`) settled
it: the first soldier boarded from `UpdateTrooperAction` (0x44b36f) on BOTH
builds, but the second boarded from **`TrooperAiStep`+0x77 (0x406327)** in the
original -- a call the port did not have. `TrooperAiStep` (0x004062B0) has a
BOARD arm between the hit-react (`AiHitReact`) and the AI-mode dispatch: if the
sight context's claimed vehicle (`SIGHTC_OFF_VEHICLE`, 0x2C) is within
`AM2_BOARD_NEAR` (0x40 = 64) at `SIGHTC_OFF_VEHICLE_DIST` (0x30), it calls
`EnterVehicle(obj, veh)` and RETURNS, skipping the dispatch and the 0x540 tail.
`SargeAiStep` (0x00407020) has no such arm -- its react is followed straight by
the dispatch. The reconstruction had factored react+dispatch into one shared
helper (`AiStepReactAndDispatch`) on the belief the middle was identical "to the
instruction"; it is not, and the fold dropped the trooper's board -- so a
non-Sarge unit boarded only later, through `UpdateTrooperAction`'s tighter
blocked-by-rect path (stepPoint must enter the vehicle's hit rect), one pump
behind the original's distance-64 board. Fixed by splitting the helper into
`AiStepReact` (returns whether the kind-7 shortcut skips the dispatch) and
`AiStepDispatch`; `SargeAiStep` keeps the react-then-dispatch wrapper,
`TrooperAiStep` inserts the board between them. Replay then runs identical (no
trap past 3197, 104k+ frames). Invisible to every A/B: no scripted drive boards
a vehicle, and after boarding the rider is off-map, so the one divergent frame
self-heals on screen.
A HALF-TRACK KILLED IN A MINEFIELD WAS NEVER MARKED DESTROYED on the port (trap
pump 26439, frame 26162; repro `sessions/htmine-26439.txt`; hand-played driving
a half-track into mines). Tables identical but for the vehicle's flags -- port
0x820, hybrid 0x824, bit 0x4 = OBJ_FLAG_DESTROYED. Pinned it: at pump 26435
both read 0x820 with the half-track already dead (health 0), so the hybrid SETS
0x4 during the death sequence and the port never does. OBJ_FLAG_DESTROYED is
set in exactly ONE place in the image -- `DestroyObjCommon` (0x00429399,
`or ebx,4`) -- reached through DestroyByType. StepType3's dead-vehicle path
dispatches on the vehicle kind through a jump table (0x0045D74C -> 0x0045D954)
and EVERY arm ends in DestroyByType: kinds 0-3 and the default share a
LeaveRemainsRow + DestroyByType tail (0x0045D777), kind 5 destroys with no
remains (0x0045D7AE). The reconstruction had played the kind's death sound and
`break`-ed, calling DestroyByType only in the default arm -- so a dead vehicle
of kind 0/1/2/3/5 was never destroyed. This one CASCADES rather than staying
cosmetic: DamageObject early-returns on `flags & DESTROYED`, so without it the
port keeps re-damaging the dead wreck as more mines go off. Fixed by giving
every arm the destroy tail. The replay then runs identical (no trap past 26162,
27k+ frames). Invisible to every A/B: no scripted drive kills a vehicle, so the
whole dead-vehicle dispatch is cold -- found by tracing the single DESTROYED
setter back through its callers once the flag was the only thing left differing.
A HALF-TRACK UNDER A MOVE ORDER SWUNG ITS AIMED GUN BACK TO THE HULL on the
port where the hybrid kept it pointed (trap pump 3552, frame 3319; 153 px, box
218,222-233,255 -- the mounted gun's barrel angle; repro
`sessions/halftrack-3552.txt`; hand-played driving a half-track). Object tables
identical; the divergence was the turret facing (o+0x530: port 0xf9, hybrid
0x08) and behind it the turret target r[1] (o+0x579: port 0xb1 == the hull
target r[0], hybrid 0x08 independent). `Step3RouteAndBoard` (0x0045D4B0)
branches on o+0x10C into two mutually-exclusive paths: with a route in progress
(o+0x10C != 0) it runs AiRouteToward and ONLY an arrival check, jumping to the
tail if not arrived (0x45d4d9); the heading/turret block -- `*r=a; r+8=4;
if (o+0x548==0) r[1]=r[0]` and the tolerance ladder -- runs ONLY when
o+0x10C == 0 (0x45d51a). The reconstruction had folded them into one
`if arrived / else`, so the `r[1]=r[0]` (turret tracks hull) ran DURING a route
too, dragging the manually-aimed gun back onto the hull. Split the branch to
match; the replay then runs identical (no trap past 3319, 20k+ frames). Only a
multi-part vehicle (a turret) under a move order shows it, and the turret
facing is not in any object dump -- found by reading o+0x530/r[1] over the
sockets after the tables came back clean.
BOARDING A JEEP AUTO-FIRED ITS MOUNTED GUN on the port where the hybrid never
fired (trap pump 2276, frame 2100; 1 px at first, then a stream of missiles;
repro `sessions/jeepfire-2276.txt`; hand-played boarding a jeep with the fire
button held). The only object difference was an extra type-5 (missile) on the
port -- 0x3f7, then 0x3f8..0x3fc as it kept firing. Traced by tracing object
registration (AddToItemList) to its type, then FireWeapon: the port's
Step3Drive fired the jeep's weapon (r+4 == 1) every cooldown while the hybrid's
did not. Every readable input matched -- weapon uid, WEAPON_FN_SLOT3, cooldown,
mouse button/changed, menu row 3, MOUSE_GRAB -1, boarding state -- so the
divergence was the ONE unreadable local, `steered`. Step3Input's mouse-fire
gate is `if (row != 1 && steered != 1) return`, and `steered` is set by the
four steering KEYS only (0x0045C1B8/C227/C257/C285); the mouse block does not
touch it (the sole `[esp+0x18]` reference in the block is the read at
0x0045C581). The reconstruction had a spurious `steered = 1` at the end of the
mouse block, so with the fire button held the mouse-fire path fired at HUD row
3 with no steering key down. Removed it; the replay then runs identical (no
trap past 2100, 20k+ frames). Invisible to every A/B: no scripted drive boards
a weaponised vehicle and holds fire, and `steered` is a stack local no dump can
show -- it was found only by eliminating every global the gate reads.
THE SQUAD PANEL'S "MV" READ 4 FOR THE JEEP where the hybrid read 5 (trap pump
2393, frame 2333; ~23 px, box 576,311-580,318; repro `sessions/jeepMV-2393.txt`;
hand-played boarding a jeep). This corrects the per-kind speed-table fix below
(the truck MV): that fix read the table's base as esp+0x58 == the init level's
+0x28, and the jeep (VEHICLE_OFF_KIND 0) exposed that the base is actually
+0x2c. Both offsets give the convoy truck (kind 3) the value 4 -- +0x34 holds
a real 4, +0x38 an uninitialised slot that is a stable 4 in play -- so the
truck stayed frame-exact and hid the off-by-one. With base +0x2c the table
reads +0x2c,+0x30,+0x34,+0x38,+0x3c = {5,3,4,4,2}, so kind 0 (jeep) -> 5 and
kind 3 (truck) -> 4. `kVehicleSpeed` is now `{5,3,4,4,2,4}`; the jeep replay
runs identical (21,572 frames) and the truck replay stays identical. The
lesson: a table index verified on ONE input pins the base only up to slots
that happen to share a value -- board a second kind before trusting the base.
EXITING A VEHICLE DIVERGED TWO WAYS AT ONCE, both on the same frame (trap
pump 4557, frame 4514; repro `sessions/vehexit-4557.txt`; hand-played getting
Sarge out of the convoy truck). The object table showed only the truck's pos
off by 1, but the render differed by ~9k px: the SQUAD panel was empty on the
port where the hybrid showed Sarge, and the truck's health bar was missing.
Two independent bugs:

  1. THE VEHICLE DID NOT STOP FOR THE UNIT LEAVING IT. Same signature as the
     tree: port `FIELD_44`(speed)=0x1c/`0xD8`(blocked)=0, hybrid 0/1. But this
     time `VehicleBlockWeight` returned 15 on the hybrid and 0 on the port for
     BYTE-IDENTICAL inputs (facing=9, at=2071..; traced via item.cpp + a hybrid
     trampoline over 0x0045BC70, reverted). The block came from Sarge, freshly
     exited, sampled at a mask point -- a type-2 trooper. `BlockWeightTroops`'
     trooper arm was INVERTED: the original blocks an ALLIED trooper and passes
     an enemy (0x0045B877 `test eax; je w=0; else mov eax,0xF` -- a vehicle
     stops for its own side and runs an enemy over), and the reconstruction had
     `ObjsAreAllied(f,o,0) ? 0 : AM2_BLOCK_FULL`, backwards. Fixed by swapping
     the ternary. The header comment was confidently wrong too and is fixed.

  2. THE SELECTION LIST LOST THE OCCUPANT. On exit the original re-selects the
     unit (`SelectUnit(occupant); DeselectUnit(vehicle)` in ExitOneFromVehicle,
     which both games reach identically), leaving the squad panel showing
     Sarge. The port left the selection EMPTY. Traced (item.cpp/objtable.cpp
     SEL/DESEL traces + hybrid trampolines over SelectUnit/DeselectUnit,
     reverted) to `OnSelectionChanged`'s "leader becomes element zero" SWAP,
     which the original does as `edx=uids[0]; ecx=uids[leaderIdx];
     uids[leaderIdx]=edx; uids[0]=ecx` (0x00427B1B). The reconstruction read
     only uids[0] and wrote it to BOTH slots, never reading the leader -- so
     whenever the leader was not already at index 0 (the occupant is appended,
     then promoted), it DUPLICATED element zero and dropped the leader. The
     matching DeselectUnit removes every copy, emptying the list. Fixed by
     reading both ends before writing either.

The replay then runs identical (9,269 frames, no trap). Both are invisible to
every A/B: no scripted drive boards a vehicle, so the trooper-block arm and the
non-first-leader swap are never exercised. Bug 2 is the sharper lesson -- a
swap that reads one end twice is a duplicate, and it only bites when the two
ends differ, which single-selection never does.
THE CONVOY TRUCK DROVE THROUGH A TREE on the port where the hybrid stopped
(whole play area diverged, ~158k px, box 0,21-479,479; trap pump 34847, frame
34786; repro `sessions/truckTree-34847.txt`; hand-played hitting a tree with
the truck). Only one object differed -- the truck (uid 3f6), pos y 2670 (port)
vs 2672 (hybrid), a 2-unit MoveStepPoint step -- and the camera follows it, so
the whole screen moved. The port's Step3Drive block check read
`FIELD_44`(speed)=0xa0/`0xD8`(blocked)=0 where the hybrid read 0/1: the port
did NOT detect the tree. A VBW trace (item.cpp + a hybrid trampoline over
0x0045BC70, both reverted) showed VehicleBlockWeight itself was correct --
both returned 15 (blocked) for `facing=1f` -- but the port's Step3Drive block
check passed `gunFacing=0` where the original passed 0x1f, so it sampled the
wrong mask direction and missed the tree. An S3D entry trace
(0x0045CB30 trampoline) traced that to `out[0]` (the record's wanted facing):
port 0x1b vs hybrid 0xfa, with the candidate counter `+0x574` advanced to 2 on
the port and 0 on the hybrid. So the port ran the AI CANDIDATE SEARCH on the
PLAYER's vehicle. Root cause: `Step3ChooseFacing` (0x0045C8D0) has THREE early
returns and only the third was reconstructed. The missing second --
`if (ListFirstField548(obj) && obj[OBJ_OFF_FIELD_10C]==0) return` -- is what
skips the search for a player-driven vehicle; without it the search fought the
player's input and, at the tree, chose a facing that was not blocked, turning
the hull aside (gunFacing 0x1f -> 0). Fixed by adding the two missing guards
(the MP-ownership guard too, for faithfulness); the replay then runs identical
(51,608 frames, no trap). Invisible to every A/B: no scripted drive boards a
vehicle and drives it into an obstacle, and ADDR_MP_SESSION is 0 here so the
first guard is inert regardless.
THE SQUAD PANEL'S "MV" STAT read 3 cm/s on the port where the hybrid read
4, after Sarge boards a vehicle (~23 px in the HUD; trap pump 46446, frame
45681; repro `sessions/vehicle-46446.txt`; digit at ~576,309, the Convoy
Truck readout). VEHICLE_OFF_KIND (0x52C) was byte-identical (3) on both, so
this was NOT a raw-field divergence -- the original does not print the kind.
`HudSquadDetail` (0x00416340) builds a per-kind SPEED TABLE on its stack at
the top of the function (0x0041634c..0x00416502: eax=4 written to slots 0,1,3,
then 1<-5, 2<-3, 5<-2) and prints `table[kind]` (0x00416bc7: `mov edx,
[esi+0x52c]; mov eax,[esp+edx*4+0x58]`). Fixed by indexing a `kVehicleSpeed[6]`
table in the vehicle arm; the replay then runs identical (103,614 frames, no
trap -- the full recording). Invisible to every A/B: no scripted drive boards a
vehicle and opens the squad panel, and the KIND field the code reads is
identical -- only the table LOOKUP the original interposes differs. (The base
offset was read as S+0x28 here, giving `{4,5,3,4,uninit,2}` -- corrected to
S+0x2c below once a jeep was boarded, since both offsets happen to give the
truck 4.)
RALLIED FOLLOWERS FIRED AT NOTHING -- a following trooper targeted and shot a
dropped weapon on the ground when no enemy was in sight, on the PORT only
(~514 px, box 204,198-238,242; trap pump 11074, repro
`sessions/rallyfollow-11074.txt`). At pump 11073 the WHOLE object table was
byte-identical (1616=1616); at 11074 follower 3fb (aimode 3) acquired target
0x800009b7 (a type-4 weapon) and fired missile 3fd, while the hybrid found
nothing. `SightScan` (0x00403B40, ours) returned the weapon as its `alt`
pickup fallback; the original returned NULL. The cause: `TrooperBuildContext`
(0x00404730) passed SightScan's sixth arg -- the SARGE flag -- as
`(int32_t)anchor` (a packed point, always non-zero) instead of ARG3 `sarge`.
espmap: the original reads `[esp+0x1c]` at 0x00404942 = ARG3, one slot past the
anchor it had stored at [esp+0x18]. That flag gates SightScan's weapon arm and
its hittable-vs-live predicate, so with it always set every follower scanned
for weapons and engaged them; only Sarge should. Fixed by passing `sarge`; the
other four SightScan callers already pass 0 correctly. Replay then runs clean
past 11074 (53k+ frames, no trap). The wrong-argument class again -- a
plausible nearby value (anchor) for a function argument -- invisible to every
A/B and to `checkoffsetuse`. (Distinct from the rally CRASH below, which was a
NULL guard; this is why the crash had to be fixed first to even reach it.)
RALLY WITH TROOPS NEARBY crashed the PORT (not the hybrid) -- a NULL dereference,
SIGSEGV at eip 0x00741BFB addr 0x4, hand-playing under `tools/sidebyside.py`
(pump 34013; repro `sessions/rallycrash-34013.txt`). nm put the fault in
`AiApproachLeader` (0x00405DB0), which rally runs on nearby troops. The
`AM2_SIGHTC_PROMOTE_FOUND` macro copied `found->OWNER` (found = ctx +
SIGHTC_OFF_FOUND) with NO null check, and rallying with nothing in a unit's
sight reaches the promote tail with FOUND null -> `*(NULL+4)`. The original
GUARDS it: `mov eax,[esi+0x20]; test eax,eax; je` skips the whole promote block
at 0x0040605A and 0x0040609E (both macro sites). Fixed by wrapping the macro
body in `if (found_)`; the sibling `AM2_ROACH_PROMOTE_FOUND` was already guarded
externally at all three of its sites, so it needed nothing. Replay then runs
clean past 34013 (61k+ frames, no fault, no trap). A NULL-guard the port dropped
-- invisible to every A/B, since no scripted drive rallies with an empty sight.
PLACING A MINE put it a few pixels off on the port (~91 px, box 254,231-273,246)
-- reached hand-playing under `tools/sidebyside.py` (trap pump 6735, frame 6412;
repro `sessions/mineplace-6735.txt`). The mine (uid 461, type-1 item) was planted
at a point differing by (9,8): port Sarge+(19,-16), hybrid Sarge+(28,-8), same
Sarge pos/pose/facing/RNG. A WATCHLAY trace (item.cpp CreateWatchedItem + a
hybrid CreateWatchedItem trampoline, both reverted) showed the muzzle `at`, the
sprite and its attach IDENTICAL on both -- the ONLY difference was the fourth
argument to `CreateWatchedItem`: hybrid 191 (Sarge's OBJ_OFF_FACING 0xbf), port 0
(his OBJ_OFF_ARMY). `FireWeapon` case 11 (kind 11, lay charge) and case 12
passed `OBJ_OFF_ARMY` (0x10) where the original pushes `[esi+0x40]`
OBJ_OFF_FACING at 0x0045FB4E/0x0045FB84; `CreateWatchedItem` uses it only to drop
the charge Cos8/Sin8 behind the muzzle, so the wrong field planted every mine due
east instead of behind the firer. Fixed by passing OBJ_OFF_FACING in both arms;
the replay then runs identical (51k+ frames, no trap). The `ADDR_ENTER_VEHICLE`
class -- a wrong-field argument -- invisible to every A/B (no drive lays a mine)
and to `checkoffsetuse` (the offset is present, just the wrong field).
DROPPING A HELD WEAPON left it on the map at the wrong place on the port
(~559 px, box 220,234-260,269) -- reached hand-playing under
`tools/sidebyside.py` (trap pump 15230, frame 15190; repro
`sessions/weapondrop-15230.txt`). `PlaceObj` (0x00429220) had its early-return
condition INVERTED: the original returns when the object is already at `where`
and NOT destroyed (already placed, nothing to do -- `test [esi+8],al`, al=4=
DESTROYED, `je RETURN` on the clear bit) and PROCEEDS when destroyed, which is
the re-place/revive case. A dropped weapon is destroyed and its pos is dragged
to the trooper every step, so pos==`where`; the reconstruction returned on the
SET bit, skipping the re-registration, so tile/hit-rect/cells stayed stale from
where the weapon last sat while the original re-placed it and cleared DESTROYED.
Confirmed by the object diff: same uid 80000618 type-4, same ammo (0x3a) and pos
(2972,3364) on both, differing only in flags (port 5 with 0x4 set vs hybrid 1),
tile and hit rect. Fixed by inverting the test in `item.cpp` `PlaceObj`; the
replay then runs identical (23,526 frames each, no trap). Invisible to every
A/B and to the offset checks -- a destroyed object re-placed at its own position
is only reached by a hand drop, and no scripted drive drops a weapon.
the IN-MISSION GAME MENU showed SAVE and LOAD where the original hides them --
reached by pausing in Boot Camp under `tools/sidebyside.py` (~10k px over the
button column, port 6 buttons vs hybrid 4). `DlgGameMenuConstruct`
(0x00452AA0) skips the SAVE (i=1) and LOAD (i=2) rows when either
ADDR_MP_SESSION or ADDR_WIN_ENABLED is set -- the original does
`cmp [0x00511DA0],0; jne` then `cmp [0x00512304],0; jne 0x00452C82` right after
RETURN, jumping clean past both `new`s -- and both are set in Boot Camp, so its
menu is RETURN/CONTROLS/AUDIO/ABORT. The rows below a skipped pair move up (the
top is a running counter), so the reconstruction's plain six-row loop drew all
six at the wrong offsets. Now gated with a running `row`; identified by `ctl
widgets` diffing the two dialogs (same object, 6 vs 4 children). Invisible to
every A/B because the game menu needs ADDR_GAME_STATE 2, unreachable from the
title.
the TITLE MENU built a different button set -- reached at once under
`tools/sidebyside.py` (~pump 684, ~10k px over the button column). The port
built the MULTI-PLAYER button on a coin toss because
`src/standalone/runtime.cpp`'s `restore_multiplayer` was defined `void` while
`src/inject/restore.h` declares it `int` and `OpenTitleScreen` calls
`!restore_multiplayer()` -- so it returned whatever was in eax. Both games now
build the same six buttons with identical sprite ids. Nondeterministic, which
is why the title matched some runs and not others; `ctl widgets` diffs it
exactly. Fixed by giving the standalone stub the injected side's body (off
unless AM2_MULTIPLAYER=1).
a click-to-move on the player faced the RAW click, not the snapped move goal
(`sessions/aim-3261.txt`, trap pump 3261, 764 px, box 217,218-274,261):
found hand-playing under `tools/sidebyside.py`, reproduces headless under
lockstep with the first differing frame exactly 3261. Only Sarge (uid 0x3e8)
differed and only in his aim/route sub-state -- FACING 218(port) vs
212(hybrid). Pump 3260 byte-identical, so it is born in pump 3261 (the
button-up pump). Two false trails ruled out by measurement: the mouse button
release is pushed AND taken at pump 3261 on both (`AM2_TRACE_DI`), so not an
input-timing seam; and every decision global (cursor 354,179; viewRect
3234,2466; button; pressMs) is byte-identical, so not the input. `AM2_TRACE_ANGLE`
pinned it to the second AngleBetween (the move step reading FIELD_C0): hybrid
to=(3592,2664)->212, port to=(3587,2646)->218. The TAP arm of `Type2PlayerInput`
(0x0044A420) sets the move goal: `FIELD_C0 = at; NearestAllowedTile(o,
TileOfPoint(at), &FIELD_C0)` -- the original (0x0044AC0A) hands the snap its
OWN &FIELD_C0, so the goal becomes the nearest allowed TILE CENTRE. The
reconstruction passed `&at`, the local, so FIELD_C0 kept the raw click and the
move step faced the un-snapped point. One-argument fix; verified identical over
the recording plus 96k idle pumps, `make check` and all seven lockstep replays
still IDENTICAL. Invisible to every scripted replay -- no drive click-releases
over the map. Independent of the flame fix (8d04c7e).
a FLAMETHROWER's flame ran one animation frame AHEAD
(`sessions/flame-5197.txt`, trap pump 5197, 80 px, box 187,196-203,214):
the flame is a def-3 trail missile whose sprite comes from `TimedDirFrame`
(0x00461F90), `col = (clock - STAMP_54) / N` indexing the direction grid.
Object tables were byte-identical (1605 each) and only the row's
ROW_OFF_SPRITE differed -- port GRID[9], hybrid GRID[8], one column apart.
The original's reciprocal is `mov eax,0x51EB851F; mul ecx; shr edx,6`: the
`mul` puts the high dword in edx (a >>32) and `shr 6` takes it to >>38, and
0x51EB851F/2^38 is exactly 1/200 -- so the divisor is 200, not 100. The
reconstruction had `/100u` (the same constant's /100 form is `shr 5`), so
at elapsed 100 the port advanced the column where the original waits for
200. Under lockstep the whole recording plus 100k idle pumps now run
byte-identical; `make check` and all seven self-terminating lockstep
replays stay IDENTICAL.
a thrown grenade LAUNCHED ~20px too low (`sessions/grenade-live-16065.txt`,
pump 16053): FireWeapon's lobbed arm (item.cpp case 2/5) passed the
arc-measuring point `a` as CreateMissile's launch point, but `a` exists
only to measure the range for the speed-scale (via ApproxDist) -- the
missile LEAVES from the muzzle `from`. The original loads `at` from frame
slot +0x18 (`from`) at 0x0045F9C2, not the +0x10 that holds `a`. Passing
`a` dropped the grenade's start below the muzzle (an arc from the waist,
not over the head); `CreateMissile(..., from, ...)` fixes it. Verified: at
the throw both missiles launch at (2945,1637), object tables identical.
a map WEAPON created with the wrong ammo, then consumed and REMOVED
(`sessions/weapon-6117.txt`, missing weapon uid 80000615 at trap pump
6117): `BuildMapObjects` (map.cpp) passed `quantity=0` to CreateWeapon, so
a crate specifying NUMB=30 got the default 3 rounds, was used up and freed,
and the port was short a whole object thousands of pumps later. The
quantity is the record's NUMB field: the original reads frame slot +0x2c
(the raw `[esp+0x30]` at 0x0042D2D1 is one slot off because
SettlePointInRegion's push is still down -- espmap confirms +0x2c), which
0x0042CBF1 sets from `movsx byte [numb]`. A prior reconstruction called
that slot "a zero local" -- the SAME espmap-shift class as the grenade
bug. `CreateWeapon(..., (int8_t)r[MAPREC_OFF_NUMB], ...)` fixes it.
Verified: port weapon now quantity=30/ammo=30, object tables identical at
pump 6116, the weapon present in both.
the grenade/teardown HEAP CORRUPTION (`sessions/grenade-1845.txt`,
`sessions/missile-11062.txt`, replay headless under `AM2_HEAP_CHECK=1`):
at level teardown the small-block heap released a 1 MB reservation with
`VirtualFree(MEM_RELEASE)`, and `am2_fixed_release` (fixedheap.cpp) freed
EVERY consecutive used slot up to the next gap -- so releasing the
reservation at `0x12100000` also madvise-ZEROED the live reservation at
`0x12200000` beside it. A later `free` of a block in that zeroed region
read `sizeFront=0` and walked a null free-list link, faulting in
`crt_sbh_free_block`/`resize_block` (port) and the original's
`__sbh_free_block` (hybrid) -- both, because both call the one shared
platform release. `slot_run[]` now records each reservation's slot-count
the grenade/teardown HEAP CORRUPTION (`sessions/grenade-1845.txt`,
`sessions/missile-11062.txt`, replay headless under `AM2_HEAP_CHECK=1`):
at level teardown the small-block heap released a 1 MB reservation with
`VirtualFree(MEM_RELEASE)`, and `am2_fixed_release` (fixedheap.cpp) freed
EVERY consecutive used slot up to the next gap -- so releasing the
reservation at `0x12100000` also madvise-ZEROED the live reservation at
`0x12200000` beside it. A later `free` of a block in that zeroed region
read `sizeFront=0` and walked a null free-list link, faulting in
`crt_sbh_free_block`/`resize_block` (port) and the original's
`__sbh_free_block` (hybrid) -- both, because both call the one shared
platform release. `slot_run[]` now records each reservation's slot-count
and release frees exactly that many. Found by adding a gated small-block
validator (`AM2_HEAP_CHECK`, heap.cpp) that walks every committed page
before each free/alloc and names the first corrupted entry, then logging
`VirtualAlloc`/`VirtualFree` ranges to see the release at `0x12100000`
swallow `0x12200000`. Both recordings now run clean to 320 s with the
validator on, and the six standard lockstep replays stay IDENTICAL.
the HUD ammo count drawn one pixel left (`sessions/hudcount-2823.txt`):
the HUD ammo count drawn one pixel left (`sessions/hudcount-2823.txt`):
the original centers the count at `mid + 1` (HUD_EDGE_PAINT+0x5B1,
`lea eax,[eax+ecx+1]`) where the strip's labels use the plain `mid`, so
the count could not share EdgeText; ours did and lost the +1. Found with
AM2_TRACE_BLIT (glyphs at 630 vs 629) and a DrawTextVertical trampoline
(center 633 vs 632).
the bottom-edge terrain and the depth-120685 item order, ONE ROOT: our
`BuildMapObjects` (map.cpp) called `ApplyHeightItem` only in the weapon
arm, while the original's item arm FALLS INTO the same shared tail
(0x0042D1C3 jmps to the `ApplyHeightItem` at 0x0042D30B the weapon arm
reaches by falling through). Our item arm did `continue`, so every map
item kept height 0 instead of the terrain's, which halved its depth layer
(`ScaleBy32Blocks(0)`=1000 vs 2000) and sorted overlapping ground sprites
the wrong way. Found with `AM2_TRACE_BLIT` (identical blit params, opposite
ORDER) then `AM2_TRACE_HEIGHT` (the hybrid calls `ApplyHeightItem hin=50`
at pump 193 for the items, the port never does). Fixed by making both arms
share the height tail; `sessions/botrow-965.txt` and
`sessions/depth-120685.txt` are the repros, identical since.

one pixel of a corner where two sandbags overlap
(`sessions/corner-1276.txt`, pump 1276, pixel 479,479): the original's
depth comparator breaks an exact tie by comparing the two objects'
POINTERS, so which segment is drawn last is decided by where the heap put
them -- and the hybrid's HeapAlloc went to glibc while the port's went to
the dev arena. Not a transcription defect: the comparator was right on
both sides and the heaps were not the same heap. The platform owns a
deterministic heap now (`src/platform/fixedheap.cpp`, below) and both
games allocate identically;
the 26 pixels from frame 80 of every comparison since the first lockstep
-- the ORIGINAL's overlay blitter stores a two-pixel run at an address of
1 mod 4 with its mapped bytes exchanged (0x0041C5A6), and ours mapped in
place; reproduced, `bootcamp.txt` identical through all 628 frames;
a radar click scaled its y from the widget's own offset rather than its
rectangle's top, so the view jumped 650 pixels too far
(`sessions/click-509.txt`, exact once the terrain unit is fixed);
weapon range in the wrong slot (`sessions/shot.txt`), held click aimed at
the raw point (`sessions/shot.txt`), depth slope parsed as an integer
(`sessions/signpost.txt`), picked-up weapon left on the map
(`sessions/pickup.txt`), and the step socket's 64-note cap (a tool defect,
`sessions/pickup.txt` after a long trap).

## THE CRT IS BEING RECONSTRUCTED, under src/platform/crt

The image carries MSVC 6's C runtime: **231 functions from `0x00464416`,
42,860 bytes, 44 of them called by the game** (the rest are their
internals and the startup). The native build ran the game over glibc
behind those names, and the two are not the same runtime where it shows:
`qsort` orders ties differently, the heap hands out different addresses,
`printf` formats floats by other rules. Each is a frame the lockstep
comparison above can see. `src/platform/crt/` is that runtime function by
function, from the disassembly, under `crt_` names (the host's libc has
the real ones), reached through the `ADDR_CRT_*` seams `standalone.h`
already re-points, so the reconstruction's sources do not change.

Done (2026-09-06), each replayed in `tests/selftest.cpp` against what the
original answered under Unicorn:

- `sort.cpp`: `qsort` with its `shortsort` and `swap`, `bsearch`.
  `tools/qsortcheck.py`, 195 arrays built for ties, 1,401 rows; three of
  four mutations fail and the fourth is a theorem.
- `rand.cpp`: `rand`, `srand` over the image's seed.
- `string.cpp`: `strtok`, `strchr`, `strstr`, `strncpy`, `strncmp`,
  `_stricmp`, `_strlwr`, `strlen`, `strcpy`, `memmove`. `conv.cpp`: `atoi`,
  `atol`, `strtol`. Nine of these are in `tests/vectors.h`: 7,487 vectors.
- `printf.cpp`: `_output`, the whole state machine, with `sprintf` and
  `vsprintf` over it. `fltcvt.cpp`: `_cfltcvt` down to `$I10_OUTPUT` -- the
  twelve-byte long double, its multiply, the powers-of-ten tables read
  from the image, `_fptostr` and the three layouts. `tools/printfcheck.py`
  runs the original's `sprintf` over 1,793 formats and values -- every
  conversion and flag, and for the float ones the halves, the nines that
  carry, denormals, the extremes, an infinity and a NaN -- and all 1,793
  replay exactly. Four of six mutations fail it -- the digit rounding, the
  decimal-exponent estimate, `%p`'s precision, the "0x" a zero does not
  get. Of the two that do not, the powers-of-ten guard threshold is a
  theorem (no entry in either table sits on 0x8000), and the multiply's
  exact-half tie is a GAP: no value in the corpus lands on it.

- `stdio.cpp` and `lowio.cpp` (2026-09-06): `fopen` through `_openfile`,
  `_getstream` and `_sopen`; `fclose`, `fread`, `fwrite`, `fgets`, `fseek`,
  `ftell`, `fflush` with `_flush` and `_flushall`, `_flsbuf`, `_filbuf`,
  `_getbuf`, `_freebuf`; and beneath them the `__pioinfo` handle table --
  `_alloc_osfhnd` and its three siblings, `_read` with the text-mode CR LF
  and Ctrl-Z rules, `_write` with LF expansion, `_lseek`, `_close`,
  `_commit`, `_chsize`, `_setmode`, `_isatty`, `_dosmaperr` over the
  image's own table, `_ioinit` and `__initstdio`. The FILEs are the
  image's `_iob`, the tables are the image's, and the two initialisers run
  lazily on the first open since nothing here runs the original's
  startup. **The oracle is the hybrid, with no Wine and no emulator**:
  `build/armymen2-hybrid-dev` runs the original CRT over `src/platform`,
  and `AM2_CRTCHECK=<dir>` (`tools/crtcheck.sh`, `tools/crtcheck.py` in
  `make check`) links the reconstruction beside it and runs
  `src/hybrid/crtcheck.cpp` instead of the game: 521 scripted sequences of
  stdio calls over 19 generated files, through both stacks on separate
  copies, 40,211 calls compared on return value, errno, the FILE's fields
  and a hash of the bytes, and every file compared afterwards. 0 differ.
  Eight of nine mutations fail it and the ninth is unreachable through the
  FILE layer; the letters S, R, T and D stay verified by reading.

- `dir.cpp`, `time.cpp`, `env.cpp` and `mbcs.cpp` (2026-09-06): the
  find family, `_chdir`, `_getcwd` with `_getdcwd` and `_validdrive`,
  `_mkdir`, `_rmdir`, `remove`, `_chmod`; `time` with `__loctotime_t`,
  `__tzset` (the TZ parser and the `GetTimeZoneInformation` arm),
  `_isindst`, its `cvtdate` and the FILETIME conversion `_findfirst`
  uses; `getenv` over `_environ`, `_mbsnbicoll` and `_mbctoupper`. The
  same hybrid check covers them, run under three TZ settings, 308 values
  each: eleven of fourteen mutations fail and the three that pass are
  theorems on this platform, listed in `tools/crtcheck.py`. Two things
  the native build cannot have without the original's startup: an
  environment table, so `getenv` answers NULL there and `__tzset` always
  takes the API arm; and the `_mbctype` tables, so `_mbctoupper` answers
  its argument. Both are stated in the modules.

The game's startup, heap, stdio, directories, `time`, `strtod`, `sprintf`, `qsort`, `bsearch`,
`rand`, `atoi`, `strtol` and the string functions above now run on the
CRT in both the standalone and the native build; the lockstep comparison
is unchanged by all of it, as it should be. `standin.cpp` holds what the
modules need and nothing has read yet -- `malloc`, `calloc`, `free`,
`_amsg_exit`, `__crtCompareStringA` and `__wtomb_environ` over the host --
and says so in its name.

- `strtod.cpp` (2026-09-06): `strtod` through `_fltin2`, the twelve-state
  parser `__strgtold12`, `__mtold12`, `__ld12tod` and `__ld12cvt` with its
  seven mantissa helpers; `_isctype` beside them in `conv.cpp`. The hybrid
  check's third section compares it on 1,616 strings by the double's bits,
  the end offset and errno, in all three TZ runs: seven of nine mutations
  fail, one is a theorem (a denormal is never observable, strtod answers
  zero for it) and one a gap (the 25th digit's rounding of the 24th, a part
  in 10^24). `memcpy` joined `tests/vectors.h` with 75 vectors, and the
  check's fourth section overlaps it 8,424 ways, which vectors cannot:
  the original copies from the end when the destination lies inside the
  source, so it is memmove under the other name. Adding it found a hole
  in the vector harness rather than in the CRT: Unicorn keeps EFLAGS
  between runs, a faulted try left the direction flag set, and 117
  vectors of two functions were recorded wrong; CLAUDE.md has it.

- `heap.cpp` and `exit.cpp` (2026-09-06): `malloc`, `operator new`,
  `free`, `operator delete`, `realloc`, `calloc` and `_msize` over MSVC 6's
  small-block heap -- `__sbh_alloc_block`, `__sbh_free_block`,
  `__sbh_resize_block`, the region and group allocators, `__sbh_find_block`,
  `_heap_init` -- with `HeapAlloc` above the 1016-byte threshold; and
  `atexit` with `_onexit`, `exit`, `_exit`, `doexit`, `_initterm`,
  `_endstdio` and `_fcloseall`. The whole native build runs on it now,
  the game's blocks and the CRT's own, and the lockstep A/B is unchanged.
  The oracle is exact where the FILE tables' was not: the check snapshots
  the entire small-block heap, runs one operation through each stack from
  the same state, and compares the two states' digests -- 24,279
  operations including a burst that fills three regions and empties them
  again, 0 differ, three region releases skipped because an unmapped
  region cannot be put back. Exit: handlers registered through both stacks land
  in one table, both `doexit`s run them in the same reverse order, and
  the table grows past its 32 entries. `mkglobals` rewrites the image's
  two terminator-table entries to `crt_endstdio` and `crt_seh_restore`,
  so the native exit path walks the same tables the original does.
  Thirteen mutations, all failing.

Every seam in `standalone.h` now names a reconstruction except the three
`standin.cpp` holds -- `_amsg_exit`, `__crtCompareStringA`,
`__wtomb_environ` -- and the DirectX creators, which are the platform's.
The development binary keeps its fixed-address arena for the game's blocks
(savestates need it); the CRT's own allocations there are on the
small-block heap, and the one block that could cross, `getcwd(NULL)`'s,
the game never asks for.

- `startup.cpp` (2026-09-06): `WinMainCRTStartup` up to its call of
  `WinMain` -- the version words, `_heap_init`, `_ioinit`, the command
  line and `__crtGetEnvironmentStringsA`, `_setargv` with its two-pass
  `parse_cmdline`, `_setenvp`, `__wincmdln`, `_cinit` walking the image's
  own initializer tables -- with `_setmbcp` and the multibyte tables,
  `_fpmath` with `_control87` and the FDIV test, and `_amsg_exit` with
  `_NMSG_WRITE` and the message box. The standalone and native builds call
  it from their startup object, so the tables every other module read
  lazily are the original's now, built in the original's order, and the
  x87 runs at the 53-bit precision the original set. `mkglobals` rewrites
  the initializer table's slots to the reconstructions as well as the
  terminator tables', which is how `_cinit` finds them. The check's
  seventh section compares the parser on 22 command lines both passes,
  `__wincmdln`, `_setmbcp` for nine code pages table for table, and
  `_control87` both ways round; the lockstep A/B is unchanged.

Not reproduced, and stated in the module: the entry point's SEH frame and
`_XcptFilter`, which map a machine exception to a C signal handler the
game never installs. `standin.cpp` holds the three locale wrappers and the
wide-environment conversion, each forwarding to the one kernel32 call its
original makes.

**The CRT is complete for what the game reaches, measured two ways.**
Every one of the 63 seams in `standalone.h` resolves to a reconstruction
-- the one outside the CRT set is the DirectInput import thunk, the
platform's -- and the native build plays Boot Camp under lockstep on all
of it. What the reconstruction's own source still takes from the host is
the pure string and memory functions the original mostly inlined --
`strcpy` 119 call sites, `memset` 98, `memcpy` 59, `strlen` 44, `strcmp`
26, `strstr` 18, `strcat` 17 -- whose answers cannot differ, and the
probes and dump code (`getenv`, `fopen`, `sprintf` in anim dumps and the
script sweep), which are the harness's; every `rand()` in `src/game` is
in a comment, the calls go through `am2_rand`. What remains of the
image's runtime is the C++ exception dispatcher and the locale wrappers,
which nothing reaches.

## THE ORIGINAL AND THE RECONSTRUCTION RUN IN LOCKSTEP

`AM2_LOCKSTEP=1` makes the platform deterministic: the clock is a count of
pumps (16.67 ms each, `AM2_LOCKSTEP_MS` overrides), `Sleep` on the game
thread moves it, multimedia timers fire from the pump when it passes them,
the sound is mixed on the game thread one step per pump, input comes only
from an `AM2_REPLAY` script keyed by pump number, and every frame presented
is hashed into `AM2_FRAMELOG`. Both the hybrid and the native build run it
headless under SDL's dummy drivers, so a comparison needs no display and
takes half a minute:

    tools/lockstep.sh tests/replays/bootcamp.txt
    tools/sidebyside.py --replay tests/replays/bootcamp.txt --tolerate-swaps

The second is the same comparison with both games STEP-LOCKED over a
socket and stopped, alive, on the first frame that differs, so the state
behind a difference can be read out of both at once; `--video` makes it
playable by hand, the leader's window driving both.

**Measured** (2026-09-06, `tests/replays/bootcamp.txt`: title, BOOT CAMP,
RETURN, both dialogs, then W, S and D held for 100 pumps each):

| what | result |
|---|---|
| native run against native run | identical, 628 presented frames |
| hybrid (original) against native (reconstruction) | **IDENTICAL through all 628 frames** (2026-09-07). Until then frame 80 on differed by 26 pixels, every one a swapped pair, which was the original's overlay blitter exchanging a two-pixel run at an address of 1 mod 4 -- now reproduced |
| the 26 pixels | 13 horizontal PAIRS on sandbag and hut edges, static from frame to frame, and in every pair the two colours are SWAPPED: the original has A,B where the reconstruction has B,A; more pairs come into view as the map scrolls |
| hand play, recorded and replayed (`tools/sidebyside.py --video`) | four defects in the first hour. `TrooperPickupItem`'s swap arm destroyed the held weapon where the original destroys the picked-up item, so a weapon picked up stayed on the map, drawn over Sarge. The object line's ninth field is a FLOAT (`DefObjLine`), the depth slope, parsed as an integer so a sign post drew over Sarge's rifle. And two argument slots: every weapon's range was its following field (`DefWeaponLine`, fields 4 and 5 swapped by the original), so the first shot detonated on Sarge; and a held click took its bearing to the raw point where the original uses the overlay-adjusted one (`Type2PlayerInput`). Fixed; the recorded session replays with nothing beyond the swapped pairs |
| what "the same 26 pixels" had hidden | the port never showed Boot Camp's INSTRUCTION SIGN and kept running through the fifty pumps the original spent paused on it: `ScriptNameUid` indexed the name table with the call that reallocates it, and `showsign`, a forward reference on a growth boundary, got uid 0. Fixed; found by the side-by-side, not by the tables, which agree because nothing moves in a sign |
| the object tables | identical at the mission start and after the walk, all 1,610 lines, Sarge at `1743,976` on both |
| the game's memory at Boot Camp | carried span 2,012 KB (74 KB of it initialised), game heap 14,037 KB, resident total 30.9 MB and FLAT over 56,000 frames |

**The lockstep sound was a leak, in both builds equally.** `am2_audio_step`
mixed one pump of sound on the game thread and QUEUED it into the SDL
stream, and a device drains a stream in real time while the pump outruns
real time whenever it is not sleeping -- so on the dummy device nothing
drained it at all: 5.6 KB a pump, 70 MB every 12,400 frames, 528 MB resident
after ninety thousand. The first reading of the game's memory was that
number and not the game's. A stack trace on the megabyte `mmap`s named it
(`strace -k`), and the CRT's small-block heap held one region throughout.
The mix is what a comparison wants, so it is mixed and dropped; the device
is opened so the game believes it has one.

So the simulation is in lockstep with the original over this drive, and the
whole visible difference is one rare two-pixel case in something that
encodes or paints terrain -- a 16-bit unit written with its bytes reversed
where the two pixels differ, invisible wherever they are equal. Not yet
located; the way to locate it is a hybrid that installs reconstructions
one at a time, which is the next tool.

**How the native build had stopped running, and why nothing said so.** The
platform's headers are included through `-isystem`, and `-MMD` omits system
headers from the dependency files, so reordering the `IDirectDraw` vtable
rebuilt `ddraw.o` and not `device.o`: the reconstruction went on calling
slot 22 for `SetDisplayMode` and landed on `WaitForVerticalBlank`. The
build is `-MD` now and the dependency files name the platform headers. A
header change that rebuilds one side of an interface and not the other is
a crash that reads like a code defect; the bisection that found it reverted
the header and got a clean run from objects that had never been rebuilt.

## THE ORIGINAL EXE RUNS OVER THE PLATFORM LAYER, WITHOUT WINE

`make hybrid` builds `build/armymen2-hybrid` and `build/armymen2-hybrid-dev`:
a PE loader of our own (`src/hybrid/loader.cpp`) that maps the retail
`ArmyMen2.exe` at `0x00400000` inside an i386 ELF whose own text sits at
`0x00700000`, binds every one of its 171 imports to `src/platform`'s
implementation by module and name, gives each thread a TEB for the SEH chain
at `fs:[0]`, and calls the MSVC CRT's entry point. No reconstruction is
linked in: it is the ORIGINAL code over OUR platform, which is the other
half of what the native build checks.

    make hybrid
    AM2_GAMEDIR=".wine/drive_c/GOG Games/Army Men II" build/armymen2-hybrid -nointro

`AM2_LOG=<file>` captures the game's log (the retail logger stub is
detoured, as the harness detours it); `AM2_EXE` names another image. The
dev binary carries the harness's control socket on port 31337, so
`tools/objdump.py --port 31337 --table` and `tools/am2ctl.py` work on it.

**Measured on Xvfb `:99`, against the original under Wine driven the same
way** (2026-09-06):

| step | evidence |
|---|---|
| imports | `imports: 171 bound, 0 missing` |
| title screen | **208 of 307,200** pixels differ from the Wine title frame, all inside the pointer's box |
| BOOT CAMP briefing, HQ dialog | `lines: 101  tokens: 372  names: 43  compounds: 16`; the HQ dialog frame differs from a Wine frame of the same drive by **118..172 pixels, all inside the pointer's 21x22 box** -- see below for what those are |
| the instruction sign | **0 of 307,200** pixels differ from the Wine frame, seven captures in a row, both pointers parked at (600,400) |
| live mission, object state | `tools/objdump.py --table` on both: **1,610 lines, no difference**; the live frame differs by 80 pixels in the minimap and the pointer, which animate |
| live mission, movement | two seconds of S moves Sarge from `pos=1674,910` to `pos=1704,972`, and only Sarge's two rows change in the table |
| the game's log | the same lines as the Wine log, harness lines aside, bar `Missing cpuinf32.dll` for Wine's `system speed: 1` -- the platform has no cpuinf32.dll to load, as the native build has not |

Two things it found on its first run, both in the platform layer and both
invisible to the native build:

- **The platform's `IDirectDraw` vtables were not in the SDK's slot order.**
  `ddraw.h` put `SetDisplayMode` last so one macro could declare the prefix
  the two interfaces share, on the reasoning that the reconstruction reaches
  every method by name. The original indexes by NUMBER: `InitDirectDraw`
  called slot 21 of `IDirectDraw2` with six dwords, got
  `WaitForVerticalBlank` taking three, and returned twelve bytes off into
  its own HWND argument. `tools/checkvtables.py` compares all thirteen
  platform vtables with mingw's SDK headers slot by slot and is in `make
  check`.
- **stb_truetype is not Wine's FreeType.** Every non-pointer pixel that
  differed in the HQ dialog -- about 1,400 -- was GDI text: the game builds
  its three fonts by drawing each character with `TextOutA` and encoding the
  result. `tools/glyphdump.py` reads those built fonts out of the original
  under Wine (all three: ArialNarrow 12 and 14, ArialBlack 18, 672 glyphs)
  into `tests/glyphs-reference.txt`, and `src/platform/glyphs.inc` is
  generated from it; `gdi32.cpp` replays the recorded bitmaps for those
  faces and heights. With that the text is byte for byte the original's, in
  the native build as well.

**The pointer's box is history, not rendering.** `DrawMenuCursor` saves and
restores only the 12x16 arrow through a 32x32 slot (AM2_BLT_TRACE=1 shows
the two stretching blits); the bubble and the bar beside it are software
sprites that nothing restores, so what lingers at a spot the pointer has
left is whatever was drawn there last, and the two drives were not
frame-synchronised. Measured rather than assumed: moving both cursors away
leaves residue on BOTH sides, the same residue the box differed by, and a
screen that repaints over the spot (the sign) is exact. The stretch itself
now uses wined3d's truncated 16.16 stepping rather than exact division,
which differs on the restore's vertical axis.

What is NOT verified here: audio (the dummy driver), the movie (Smacker is
stubbed), and every screen the drive above does not reach.

## THE GAME RUNS AS A LINUX EXECUTABLE

`make native` builds `build/armymen2` and `build/armymen2-dev`: the same
reconstruction as the
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

**NO VEHICLE WAS EVER STEPPED BY THE PLAYER OR THE AI, and the fixed
point found it in an afternoon.** Reported as "movement is completely
broken inside a vehicle, and the sound is wrong until Esc opens the menu",
with two F5 saves, one before boarding and one after. Loading the
before-save in the native build and in the original (`tools/enterlevel.sh`),
clicking the truck and holding D: the original's truck turns on boarding
and again under D, ours never changes facing. Same controlled-object
context on both, same guards clear, so the vehicle input handler was
not being reached -- and `grep` found no call to `Step3Input` anywhere,
only its definition, while its own header names one caller. `StepType3`'s
alive path was misread in four places against 0x0045D660:

- it tested the record's first word, the heading byte, where the original
  tests `OBJ_OFF_FIELD_59C`, so every live vehicle skipped the whole block;
- a vehicle with occupants went nowhere: the original sends the player's
  vehicle (first occupant owned by the player) to `Step3Input` and
  `Step3RouteAndBoard` and everyone else's to `AiStep`, then all of them
  to `Step3ChooseFacing`; ours had the attach arm only, for empty vehicles;
- a dead kind-0 vehicle went to the attach arm instead of being destroyed;
- the per-frame copy of the aim point into the vehicle's fire point, gated
  on the record's second word, was missing.

Measured after the fix on one save, D for two seconds then W for four: the
native truck turns 33 to 177 to 65 and drives from (1242,2659) to (632,2710)
with the record in the driving state 4, and stops on release. No A/B could
have seen it: `bootcamp`'s dump is taken at the briefing before any vehicle
steps, `mission` compares logs, and Boot Camp's truck sits in the motor pool
either way. The vehicle sound is the same fix's business -- the engine's
two sound tables key on the record state that was stuck at 1 -- and wants
retesting on the desktop.

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

**TWO NATIVE BINARIES: THE PLAYER'S AND THE DEVELOPER'S, and `make native`
builds both.** It builds `build/armymen2` with no control socket, no injected input and no
savestates, and `build/armymen2-dev`, the same sources
with `AM2_DEVTOOLS`, plus the harness's control.c and input.c and
`src/standalone/devtools.cpp`. The socket is on by default there (port
31337, `AM2_CONTROL=0` turns it off), and everything a drive or a dump
needs lives in that binary only.

**SAVESTATES, in the dev binary.** Every game allocation there comes from a
96 MB arena at a fixed address (0x0C000000 now; see the next section) with a deterministic first-fit
allocator whose bookkeeping lives inside the region, so a savestate is the
carried globals (2 MB, 0x0046F000..0x00666000) plus the arena's used part,
16 MB in a Boot Camp mission, written and restored as bytes.
`snap save FILE` and `snap load FILE` on the socket (FILE absolute -- the
game chdirs). They run on the game thread at the top of the host's frame
pump, never mid-frame. **F5 and F9 are the GAME's save and load, not the
raw state**: F5 calls `SaveGame` with the first free `f5-N.sav` under the
folder the game-proc block names and prints the path, F9 makes the LOAD
button's writes for the latest one -- because a game save is the file
`tools/enterlevel.sh` brings back in any build and any run, which is what
a fixture has to be. Measured: save
at (1981,1026), walk to (2249,998), load, read (1981,1026), walk again and
the game carries on. What a state does NOT hold is anything the platform
owns -- surfaces, sound buffers, open files -- so it is valid within the
session and mission it was taken in; the game's own SAVE GAME is the
portable one.

**THE ARENA IS THE PLATFORM'S NOW (2026-09-07), AND EVERY BINARY HAS
IT.** `src/platform/fixedheap.cpp` is the heap under the CRT's `HeapAlloc`
and the reservations under its `VirtualAlloc`: a first-fit heap over 96 MB
at 0x0C000000 and 1 MB slots over 64 MB at 0x12000000, both at fixed
addresses, both mapped `MAP_NORESERVE`. The dev binary's own arena and
its `AM2_DEV_NOARENA` switch are gone with it; the game's allocations go
through `crt_malloc` in both native binaries, and the hybrid -- running
the ORIGINAL CRT over the same `HeapAlloc` -- lands every object at the
same address the port does. That is what the corner divergence above
needed: the depth comparator's pointer tie-break. `AM2_FIXED_HEAP=0` is
the bisecting switch (glibc and `mmap(NULL)` again). The savestate
snapshots both regions, so a `snap` carries the small-block heap too.

**THE ARENA'S ADDRESS IS NOT A FREE CHOICE.** The game overloads fields
with a uid or a pointer and tells them apart by value; uids carry their
kind in the high nibble (`200003E8`, `800003E9`), so the arena has to sit
where the MSVC heap and glibc's brk heap both do, below 0x10000000. The
other switches stay in:
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

**BOOT CAMP CAN BE SAVED ONCE IT HAS A FOLDER, which the menu never
gives it.** `SaveGame`'s guard is the game-proc block's first string, the
player's name, which is also the folder the save goes under; the level's
name (`bootcamp`, `kitchen`) sits 0x20 further into the block. In Boot
Camp the first string is EMPTY -- there is no player -- so the original
cannot save there and the menu never offers it. Write a folder name into
that string and the save dialog saves Boot Camp's briefing: 1,609 items,
347 KB, under `save\<that name>`. The dev binary's F5 does exactly that,
using the level's name when the folder is empty, and `loadgame bootcamp
FILE` at the title screen brings the file back identically in all three
builds. (An earlier version of this paragraph said the block already held
`bootcamp`; it was the LEVEL string, at +0x20, being read.)
The raw savestates do not cross runs: a
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

Nothing uncommitted. **1,643 patches plus 6 REGISTERED**, **67** analysis
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
