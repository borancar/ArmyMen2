# vc6host — the Visual C++ 6.0 toolchain, natively on Linux, no Wine

The same trick as the game's hybrid loader (`src/hybrid/loader.cpp`), pointed at
the compiler instead of the game: map a Win32 PE at the base its pointers were
written for, fill its import table with our own Linux implementations of the
Win32/CRT functions it names, and jump in. `CL.EXE`, `C1/C1XX/C2.DLL`, `LINK.EXE`
and `MSPDB60.DLL` are all i386 PE32 images at fixed, non-overlapping bases that an
i386 Linux process leaves free, so no relocation is needed.

Why: to recompile this reconstruction with the *exact* original MSVC 6.0 compiler
and byte-match the shipped `ArmyMen2.exe` — the ultimate correctness check.

## Files

- **`vc6host.cpp`** — the loader. Maps a PE, binds its imports (via the shim, and
  by hosting further toolchain DLLs like `mspdb60.dll`), sets up a TEB in the FS
  segment, runs the entry (`DllMain` for a DLL), and provides
  `LoadLibrary`/`GetProcAddress`/`_spawnv` so `cl` can drive its passes. `cl`
  spawns `c1`/`c1xx`/`c2` as "processes"; we bridge that to each DLL's
  `_InvokeCompilerPass@12`, and pass the compile flags through `MSC_CMD_FLAGS` as
  `cl` does. Real EXEs (`link`) are re-exec'd under a fresh `vc6host`.
- **`shim.c`** — Linux implementations of the ~250 KERNEL32 / USER32 / MSVCRT /
  ADVAPI32 functions the toolchain imports (file I/O with `\`→`/` path
  translation, an MSVC-layout `FILE` stdio, memory, MBCS/ctype as single-byte,
  `_ftol`/`_setjmp3` in asm, 16-bit wide strings, a handle table, …).

## Build & use

```
make vc6                              # -> build/vc6host
export VC6_ROOT=/path/to/en_vs6_ent   # the VC6 install (default is hardcoded)
build/vc6host "$VC6_ROOT/VC98/BIN/CL.EXE" -c foo.c    # -> foo.obj
```

Env knobs: `VC6_ROOT` (install root), `VC6_BINDIR` / `VC6_MSDEVBIN` (override the
two bin dirs), `VC6_VERBOSE=1` (loader trace), `VC6_INVOKE=1` + `VC6_LOAD=<dll>`
(drive one pass DLL directly, for debugging).

## Status

- ✅ `CL.EXE` runs and prints its banner.
- ✅ `cl -c foo.c` → a valid i386 COFF `.obj` with the original MSVC6 codegen
  (`c1`/`c1xx` frontend → `c2` backend, `mspdb60` hosted).
- ☐ linking (`LINK.EXE`), then recompiling `src/game` and byte-matching the
  original binary.
