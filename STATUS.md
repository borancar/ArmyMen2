# Status

Where the reconstruction stands, so a session does not have to re-derive it.
**`CLAUDE.md` and `docs/` are authoritative**; this is the current snapshot, and
every number in it carries the command that produces it, so it can be
re-measured rather than believed. The dated log this file used to be (2026-09-04
to 2026-10-31, every finding as it landed) is in
[`docs/status-history.md`](docs/status-history.md).

Last updated: **2026-10-31**.

## At a glance

| | state | measured by |
|---|---|---|
| game functions reconstructed | **all of them**: 0 of 1,654 below the CRT frontier left in the original | `tools/remaining.py` |
| Win32 / DirectX boundary | **closed**: 131 functions, 276 sites, 0 left | `tools/coverage.py` |
| MSVC 6 CRT | **complete for what the game reaches** (63 seams) | `docs/crt.md`, `tools/crtcheck.py` |
| native Linux port | **plays identically to the original**: 16 of 16 recorded sessions frame-exact | `tools/sidebyside.py` |
| byte-for-byte match | **1,639 of 1,646** functions identical; 305 of 389,636 bytes unmatched | `tools/relinkscreen.py --baseline` |
| static checks | `make check`: every tool passes | `make check` |

## The builds

- **`make`** (= `make native`, the default goal since 2026-10-31): `build/armymen2`,
  the Linux port -- an i386 ELF over SDL3 through `src/platform/`, with no
  Wine and no original code, and `build/armymen2-dev`, the same sources plus the
  control socket (port 31337), savestates and injected input that every tool in
  `tools/` drives. Run with
  `AM2_GAMEDIR=".wine/drive_c/GOG Games/Army Men II" build/armymen2 -nointro`.
  Needs `glibc-devel.i686`, `libstdc++-devel.i686`, `SDL3-devel.i686`. Builds
  from committed sources alone; the generated inputs are refreshed by
  `make standalone-generate`, which needs the image.
- **`make hybrid`**: `build/armymen2-hybrid(-dev)`, the ORIGINAL `ArmyMen2.exe`
  loaded by our own PE loader over the same `src/platform/`, no Wine. It is the
  oracle every comparison here is made against.
- **`make vc6-relink`** (and `tools/relinkscreen.py`): the reconstruction
  compiled by the original Visual C++ 6.0 (build 12.00.8168), hosted natively
  by `build/vc6host`, laid at the original addresses and compared byte for byte
  with the image. `make vc6-link ARGS=--run` links a runnable image with the real
  `LINK.EXE`, at addresses that do not match the original's.
- **`make all`** / **`make run`**: the injected harness -- `am2hook.dll` patches
  the reconstruction into the original EXE under Wine. It is how the
  reconstruction was verified function by function, and it still builds and
  runs; it is no longer the way functions are tested.
- **`make standalone`**: the reconstruction as a Windows PE under Wine. Fails
  at link: `src/standalone/runtime.cpp` calls `am2_set_command_line` (since
  `be5c2980`), which lives in `src/platform/kernel32crt.cpp`, a file this build
  does not link. Its generator step, which `make check` uses, works.

## Coverage

**1,645 patches plus 6 REGISTERED**, **71** analysis tools in `make check`
(`tools/checkpatches.py` for the patches; `tools/checkclaims.py` counts the
recipe). The six registered functions are callbacks the game hands to Windows
(the window procedure, two timer procedures, the packet thread, two per-object heal callbacks) and
so are reconstructed without a patch.

`tools/remaining.py`: the 24 entries of `.text` that are not ours are 19 linker
thunks, 5 jump tables and 4 harness/import entries -- none a function the
original source had. **There is nothing left to transcribe.**

The original's data: every non-zero byte of its `.rdata`/`.data` (0x0046F000 to
0x0048E000) is a named, placed C symbol -- 278 of them, 243 pure data and 35
pointer tables -- and the gaps between them are zero-fill, so the port carries
no blob from the image (`tools/placement.py`, `tools/checkplacement.py`). Dead
strings are zeroed rather than carried, **960 dead ranges, 49,144 bytes zeroed**
(`tools/deadstrings.py`, re-verified by `tools/checkdeadstrings.py`), and so are
the original's widget and movie vtables, which nothing reads now
(`tools/mkglobals.py` `DEAD`). The native build still lays that data at the
original's addresses; the VC6 runnable image is the evidence the source does
not depend on it.

## The byte-for-byte match

`tools/relinkscreen.py --baseline`, then `--list`:

| | |
|---|---|
| functions compiled and compared | 1,646 (of 1,649 installed; `trig.cpp`'s `BuildTrigTables`, `Sin8`, `Cos8` are not compiled by the relink) |
| byte-identical | **1,639** |
| differing | 7, total edit distance 610 |
| original code covered | 389,636 bytes, of which **305 are unmatched** after alignment (99.92% identical) |

The seven, with what is known about each (`docs/bytematching.md`,
`docs/vc6-regalloc.md`):

| function | distance | what is left |
|---|---:|---|
| LoadMap | 356 | a missing rotation turn between SCEN's `size` read and MHDR's `lea`. Its plane and TRIG size checks stay `(int32_t)size != plane`: the image compares, and the `sub` spelling is not an option |
| RowUpdate | 154 | C2's consumed-store reload rule; no natural consumer found |
| FindPath | 32 | **register choice only** (one spill-reload register); accepted |
| SightScan | 26 | one C2 block-layout decision (jump threading by the passes at 0x107291e4/0x107198fc); no source lever found |
| ScriptParseEvent | 18 | open |
| ScriptPad | 16 | **register choice only**; accepted |
| UpdateTrooperAction | 8 | open |

**40 "number win" markers in 12 files** keep source that exists only to steer
the register allocator -- dead volatile reads, impossible `x == a && x == a+1`
pairs, a local or two -- listed with the distance each bought in
[`docs/numberwins.md`](docs/numberwins.md), to be traded for real source shapes
once the binary matches 100%. `grep -rn "number win\|impossible pair" src` lists
them. The newest (2026-10-31) is in CheatLine and is fragile by nature: which of
two loads VC6 gives `edx` there follows its symbol table, so a declaration added
to or removed from `standalone.h` can flip it -- and PlacementScreenClick, which
sits on the same tie -- either way.

How a pass is run is [`docs/bytematch-lab.md`](docs/bytematch-lab.md).

## Verification

**Side by side against the original, exact, 2026-10-31** (`tools/sidebyside.py
--replay FILE`, hybrid vs `build/armymen2-dev`, no tolerance): every recorded
session plays identical to its last frame.

| session | frames | session | frames |
|---|---:|---|---:|
| bootcamp | 628 | grenade-1845 | 1,785 |
| aim-3261 | 3,206 | grenade-live-16065 | 15,967 |
| botrow-965 | 875 | hudcount-2823 | 2,767 |
| click-509 | 436 | missile-11062 | 11,031 |
| corner-1276 | 1,251 | pickup | 867 |
| fence-edge | 1,127 | shot | 520 |
| flame-5197 | 5,116 | signpost | 880 |
| flameaim-3587 | 3,544 | weapon-6117 | 5,931 |

`depth-120685` (about two hours) was not run. Most sessions under
`tests/replays/sessions/` end on their last input rather than an `exit` line,
and the tool stops only when a game exits, so append `N exit` to a copy first
or it idles forever.

**`make check`** (2026-10-31): all 71 tools pass. Its drift step reports
`place.ld` and `origchunks.S` until the regenerated files are committed.

**The functions no drive reaches** are checked by enumerating oracles instead
(`docs/oracles.md`). **That list is now 31 of 32**, and the one remaining is
excluded deliberately: `ItemSetBox`, whose arithmetic is `RowAlloc`'s and runs
2,512 times a mission.

The injected-build A/B suite (`tools/ab.sh all`) was last run in September; see
the history. The hybrid replaced it as the reference.

## Open

- **The seven differing functions** above, LoadMap first by size.
- **`trig.cpp` is outside the relink**: `bytematch.py` skips the file, so
  `BuildTrigTables`, `Sin8` and `Cos8` have never been byte-compared.
- **The VC6 runnable image has not been run since the vtable change.** It played
  Boot Camp, the campaign and save/load matching stock on 2026-09-28
  (`src/vc6/vcdrive.py ROUTE --ab`). Its widgets now use VC6's own vtables
  instead of the cdecl forwarder tables. It compiles and links; nobody has played it
  since. The `vc6-link` comment in the `Makefile` still describes the older
  state, a fault in SetGamePalette.
- **`make standalone`'s link failure** (above).
- **Multiplayer is unexercised** on every build: nothing here opens a DirectPlay
  session, so the comm layer is verified by reading and by oracles only.
- **`make native` warnings, 7 distinct, all understood**: five
  `-Wformat-overflow` on the original's own `sprintf` buffers (`startup%d` into
  16 bytes, script paths into 64 -- a latent overflow the original has, kept),
  the deliberate 16-bit `NearestAllowedTile` view in AiTrooperStep
  (`-Wcast-function-type`), and vendored Lua's `tmpnam`.
- **Number wins** to revisit at 100% (above).

## Recent

- **2026-10-31**: the widget and movie vtables are compiler-made. The standalone
  builds make the widget classes and `AM2_Movie` polymorphic -- five virtuals on
  `AM2_Widget` (destructor, `Paint`, `Update`, `TakeFocus`, `Repaint`) with
  per-class overrides that forward to the members named for the original
  functions, and `AM2_Movie::Poll`. The 51 hand-written `am2_vtable_*` tables,
  their `standalone.h` redirects and 154 `-Wcast-function-type` warnings went;
  each compiler lays its tables out by its own ABI (Boran: "let each compiler
  default to its own conventions"). VC6 reaches the slots by index, which is its
  own MSVC-layout table; GCC makes real virtual calls (flat code through
  out-of-line `ObjV*`). The injected build is unchanged. `make native` warnings
  174 -> 7 distinct; `native` became the default goal; this file was split from
  its history.
- **2026-10-29**: every struct member named -- roles from evidence (the game's
  `aai/*.aai` column headers, script parsers, log strings), `unusedXX` for the 34
  members nothing reads (`docs/fieldnames.md`, `tools/offsetreaders.py`); the
  three pre-existing `make check` failures fixed.
- **2026-10-20 to 10-26**: the relink from 17 differing functions to 7; the
  residues of SightScan, RowUpdate and FindPath traced into C2 itself
  (`docs/vc6-regalloc.md`); FindPath and ScriptPad accepted as register-only.
- **2026-09-27**: the VC6 toolchain runs natively and the first functions
  recompiled to the shipped bytes -- 399 then, 1,639 now.
