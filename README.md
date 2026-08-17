# ArmyMenII

Reconstructing the source of **Army Men II** (3DO, 1999) and porting it to Linux.

The approach is incremental rather than big-bang: keep the original
`ArmyMen2.exe` running under Wine, and progressively replace its functions with
reimplemented ones, verifying against real gameplay at every step. Only once
enough of the engine is reconstructed does the port become standalone.

## Layout

    tools/      analysis tooling (Python, run from .venv)
    docs/       findings, plus generated .tsv datasets
    src/        reconstructed source
    build/      build output (ignored)
    reference/  scratch reference material (ignored, never committed)
    .wine/      Wine prefix with the GOG install (ignored)

Start with [`docs/00-recon.md`](docs/00-recon.md), then
[`docs/01-harness.md`](docs/01-harness.md).

## Running the harness

```sh
make run                # harness + the game's own 1999 debug logger
make run TRACE=1        # argument-trace every patched function
make run OBSERVE=1      # log call sites of the observed functions
make run GAMELOG=0      # leave the logger stubbed
make run-stock          # unpatched GOG binary, the A/B reference
```

`make run` is the only launch target — variations are knobs (`GAMELOG`, `TRACE`,
`OBSERVE`, `ARGS`, `DESKTOP`, `ID`, `ISOLATE`), not separate targets.

Runs are independent. Desktop name, control port, log file and screenshot
directory all derive from `ID`, which defaults from `$DISPLAY`. Running two
games at once additionally needs `ISOLATE=1`, because the game guards itself
with a named mutex that is scoped to the wineserver, i.e. to the prefix:

```sh
make run                  # ID from DISPLAY
make run ID=7 ISOLATE=1   # a second game, own prefix, own everything
make config ID=7          # what is that instance called?
```

Watch the recovered debug commentary from another terminal (`make config`
prints the exact path for an instance):

```sh
tail -f ".wine/drive_c/GOG Games/Army Men II/am2-0.log"
```

`launcher.exe` starts the game suspended, injects `am2hook.dll`, and resumes it,
so patches are in place before the entry point runs. No shipped file is
modified — skip the launcher and the install is stock. Set `AM2_TRACE=1` for
generic argument tracing of every patched function.

## Setup

The Wine prefix lives in-tree at `.wine/` and is not committed. Always point
Wine at it explicitly:

```sh
export WINEPREFIX="$PWD/.wine"
wine explorer /desktop=amii,800x600 "C:\GOG Games\Army Men II\ArmyMen2.exe"
```

Python tooling:

```sh
python3 -m venv .venv && ./.venv/bin/pip install pefile capstone Pillow numpy
```

`pefile` and `capstone` cover the static analysis. `Pillow` and `numpy` are
needed only by `tools/point.py`, which imports them inside the function that
locates the cursor — so a venv without them looks fine until the first time
something actually points at the screen.

Host toolchain: `i686-w64-mingw32-gcc/g++` (32-bit PE — Wine 11 here is
new-WoW64 and cannot build 32-bit winelib), `radare2`, `Xvfb`.

## Tools

| | |
|---|---|
| `tools/am2.py` | PE loader addressed by VA, resilient linear disassembly, string/xref search |
| `tools/find_savetags.py` | savegame chunk tags + the file/line anchors that bound translation units |
| `tools/functions.py` | function inventory with translation-unit attribution |
| `tools/find_logs.py` | recovers the stubbed-out debug log format strings |
| `tools/checkdetour.py` | verifies an address is safe to overwrite with a 5-byte jmp |
| `tools/checkabi.py` | audits declared calling conventions against the machine code |
| `tools/callsites.py` | exact direct call sites of a function, for observation |
| `tools/imports.py` | every use of the import table, and which function makes it |
| `tools/comcalls.py` | COM vtable dispatch — the DirectX calls no import scan sees |
| `tools/disasm.py` | one function, annotated with imports, COM slots and strings |
| `tools/am2ctl.py` | client for the control socket — drives the running game |
| `tools/drive.sh` | headless launch, screenshot and menu navigation on Xvfb |

Most write a `.tsv` into `docs/`. Regenerate in dependency order:

```sh
./.venv/bin/python tools/find_savetags.py
./.venv/bin/python tools/functions.py
./.venv/bin/python tools/find_logs.py
./.venv/bin/python tools/imports.py
./.venv/bin/python tools/comcalls.py
```

`imports.py` and `comcalls.py` together map the boundary between the game and
the outside world — the one being closed by reconstruction. Neither alone is
enough: DirectDraw, DirectSound and DirectInput are reached through vtables and
never appear in the import table, while the game's own C++ virtual calls have
the identical machine-code shape. `comcalls.py` separates them by chasing the
object pointer back to a global.

## Conventions

- Reconstructed code uses `<stdint.h>` fixed-width types throughout
  (`uint32_t`, `int16_t`, …) — never bare `int`/`long` or Win32 `DWORD`/`WORD`.
  The original is 32-bit x86 where `long` is 32 bits; the target is 64-bit
  Linux where it is not.
- All addresses are absolute VAs. Relocations are stripped from the executable,
  so it always maps at `0x400000` and addresses are stable.

## On the Army Men 1 source

The source to the *first* Army Men (1998) circulates publicly and shares much of
its engine lineage with this game — 8 of the 10 filenames recovered from this
binary also exist there. Project policy is to use it for **naming and data
structure layout hints only**. All logic is derived from disassembly of
`ArmyMen2.exe`. That material is never copied into this repository; anything
consulted stays in the ignored `reference/` directory.
