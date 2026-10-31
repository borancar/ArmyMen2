# ArmyMenII

Reconstructing the source of **Army Men II** (3DO, 1999) and porting it to Linux.

Every function of the game has been reconstructed in C++ from the disassembly of
the shipped `ArmyMen2.exe`. The result runs as a native Linux executable that
plays frame-for-frame identically to the original. The same source, compiled
with the original Visual C++ 6.0, reproduces the shipped machine code
byte for byte for all but seven of its functions.

## Build and run

```sh
make                    # = make native: build/armymen2 and build/armymen2-dev
AM2_GAMEDIR="/path/to/Army Men II" build/armymen2 -nointro
```

`make` with no target builds the native port: an i386 ELF linked against
`src/platform/`, which implements the Win32, DirectDraw, DirectSound,
DirectInput and C-runtime surface the game calls over SDL3. No Wine, and no
original code at run time.

- **You need your own copy of the game.** The port reads the original's data
  files (maps, sprites, sounds, `aai/*.aai`) from `AM2_GAMEDIR`, the directory of
  an installed Army Men II (the GOG release is what this was built against).
  Unset, it uses the working directory, as the original does.
- **Build dependencies** (32-bit, because the game's data assumes 4-byte
  pointers): `gcc`/`g++` with `glibc-devel.i686`, `libstdc++-devel.i686` and
  `SDL3-devel.i686` (Fedora names). The build compiles committed sources only;
  it does not need `ArmyMen2.exe`.
- **Two binaries.** `build/armymen2` is the player's. `build/armymen2-dev` is the
  same game plus the development hooks every tool here drives: a control socket
  (port 31337), savestates, injected input.
- **Films** (the 3DO intro, the campaign cutscenes) play through
  [libsmacker](https://libsmacker.sourceforge.net), vendored in
  `third_party/libsmacker`, behind the game's own Smacker calls
  (`src/game/win32/smackw32.cpp`). `-nointro` skips the intro.
- **The game still believes it owns the screen.** It asks DirectDraw for
  exclusive full-screen at 640x480 in 8 bits, and the platform answers
  success, enforces nothing and gives it a window instead. This is deliberate
  and is to stay: the game keeps running its full-screen code paths (a full
  256-entry DirectDraw palette among them), which are the ones every
  comparison against the original exercises. Its `-w` windowed mode exists but
  is not the port's default.
- **Window and pacing knobs**: `AM2_SCALE=N` (integer window scale; default
  the largest that fits), `AM2_RESIZABLE=1` (a free-size, letterboxed window
  instead of a fixed one), `AM2_FPS=N` (the 60 Hz pacing Flip stands for; 0
  turns it off), `AM2_VSYNC`.

## Status of the byte-for-byte reconstruction

As of **2026-10-31** (details and every command in [`STATUS.md`](STATUS.md)):

| | |
|---|---|
| game functions reconstructed | **all**: 0 of 1,654 left in the original (`tools/remaining.py`) |
| functions recompiled by MSVC 6 and compared | 1,646 |
| **byte-identical to `ArmyMen2.exe`** | **1,639** (99.6% of functions) |
| original code bytes still unmatched | **305 of 389,636** (99.92% identical) |
| native port vs the original | 16 of 16 recorded play sessions frame-exact |

The comparison is a whole-image relink. `build/vc6host` runs the original
Visual C++ 6.0 toolchain (build 12.00.8168, the build the game's own objects
carry) natively on Linux, through our own PE loader and a Win32/MSVCRT shim
(`src/vc6/`). `src/vc6/relink.py` compiles every translation unit with it,
lays each function at its original address, resolves every relocation to the
address the original linker would have written, and compares the bytes with
the shipped image. Nothing about the reconstructed targets is wildcarded.

```sh
make vc6
./.venv/bin/python tools/relinkscreen.py --baseline   # 1646 functions, 7 differ, total 610 bytes
./.venv/bin/python tools/relinkscreen.py --list       # the seven, largest first
```

The seven that still differ are LoadMap, RowUpdate, FindPath, SightScan,
ScriptParseEvent, ScriptPad and UpdateTrooperAction. Two of them (FindPath,
ScriptPad) differ only in register choice. SightScan and RowUpdate are each
traced to a single decision inside the compiler's code generator for which no
source spelling has been found. A few dozen spellings that exist only to steer
the register allocator are marked "number win" in the source and listed in
[`docs/numberwins.md`](docs/numberwins.md), to be traded back for real source
shapes once everything matches. The method is
[`docs/bytematching.md`](docs/bytematching.md),
[`docs/bytematch-lab.md`](docs/bytematch-lab.md) and
[`docs/vc6-regalloc.md`](docs/vc6-regalloc.md).

## How it got here

The reconstruction was piecewise, never a big-bang rewrite. The original EXE
kept running under Wine with `am2hook.dll` injected (`make run`), and its
functions were replaced one at a time, each verified against real gameplay
before the next. Once the Win32/DirectX boundary was closed, the same sources
linked into a standalone executable, then into the native Linux port. Two
references are used to check it:

- **The hybrid** (`make hybrid`): the *original* `ArmyMen2.exe`, loaded by our own
  PE loader over the same `src/platform/` as the port, with no Wine. Run against
  the port in lockstep, it is the behavioural oracle:

  ```sh
  ./.venv/bin/python tools/sidebyside.py --replay tests/replays/bootcamp.txt
  ```

  steps both games through the same input and stops on the first frame that
  differs, with both games left alive at that frame for inspection.
- **The VC6 relink** above: the same source, the original compiler, the
  original bytes.

`make check` runs the 71 static analysis tools and enumerating oracles that need
no running game (about 35 minutes), plus a drift check over the generated files
in `docs/` and `src/standalone/`.

## Other targets

```sh
make hybrid             # the original EXE over src/platform (the oracle)
make vc6-relink         # the byte-for-byte relink (needs VC6_ROOT, see below)
make vc6-link ARGS=--run    # a runnable image linked by the real LINK.EXE
make all                # the injected harness: am2hook.dll + launcher.exe
make run                # ...launched into the original under Wine
make run-stock          # the unpatched GOG binary, the Wine A/B reference
make check              # every static check
```

## Layout

    src/game/        the reconstructed game (C++); src/game/win32/ is the part that calls Win32/DirectX
    src/platform/    Win32/DirectX/CRT over SDL3 for the native port; src/platform/crt/ is MSVC 6's CRT, reconstructed
    src/hybrid/      the PE loader that runs the original EXE over src/platform
    src/vc6/         the native VC6 host, the relink and the LINK.EXE driver
    src/inject/      the Wine injection harness (C) and shared headers
    src/standalone/  generated and hand-written glue for the standalone builds
    tools/           analysis and verification tooling (Python, run from .venv)
    tests/           selftest vectors and recorded replays
    docs/            findings, plus generated .tsv datasets
    build/           build output (ignored)
    reference/       scratch reference material (ignored, never committed)
    .wine/           Wine prefix with the GOG install (ignored)

Read [`CLAUDE.md`](CLAUDE.md) for the working conventions, [`STATUS.md`](STATUS.md)
for where things stand, and [`docs/00-recon.md`](docs/00-recon.md) for what the
binary is.

## Development setup

Python tooling:

```sh
python3 -m venv .venv && ./.venv/bin/pip install pefile capstone unicorn Pillow numpy
```

`pefile` and `capstone` cover the static analysis, `unicorn` the emulating
oracles (`make selftest`, `tools/vectors.py`). `Pillow` and `numpy` are needed
only by the screenshot tools.

The Wine-side builds (the injected harness, the Windows standalone) need
`i686-w64-mingw32-gcc/g++`. The Wine prefix lives in-tree at `.wine/`, is not
committed, and is always named explicitly:

```sh
export WINEPREFIX="$PWD/.wine"
```

The VC6 targets need a Visual Studio 6.0 install. Point `VC6_ROOT` at it (the
directory holding `VC98/`) and build the host with `make vc6`.

## Conventions

- Reconstructed code uses `<stdint.h>` fixed-width types throughout
  (`uint32_t`, `int16_t`, …) — never bare `int`/`long` or Win32 `DWORD`/`WORD`.
  The original is 32-bit x86, where `long` is 32 bits; a 64-bit host is not.
- All addresses are absolute VAs. Relocations are stripped from the executable,
  so it always maps at `0x400000` and addresses are stable.

## On the Army Men 1 source

The source to the *first* Army Men (1998) circulates publicly and shares much of
its engine lineage with this game — 8 of the 10 filenames recovered from this
binary also exist there. Project policy is to use it for **naming and data
structure layout hints only**. All logic is derived from disassembly of
`ArmyMen2.exe`. That material is never copied into this repository; anything
consulted stays in the ignored `reference/` directory.

## License

This project's reconstructed source is licensed under the **GNU General Public
License, version 2** — see [`LICENSE`](LICENSE) for the full text.

Vendored third-party code keeps its own license: `third_party/lua` (MIT) and
`third_party/libsmacker` (LGPL 2.1 or later; see its `COPYING` and `UPSTREAM`).

The license covers the reconstruction in this repository only. It does not grant
any rights to *Army Men II* itself or its assets: the original game, its data
files, and `ArmyMen2.exe` remain the property of their respective owners, are
not distributed here, and you must own a copy to use this port.
