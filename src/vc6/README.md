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

## Byte-matching the reconstruction

The payoff (`bytematch.py`, `make vc6-match`): recompile each reconstructed
function with this host and compare its bytes to the shipped `ArmyMen2.exe`.
A match is proof the source is right; a diff is a lead -- almost always the
reconstruction is *correct* but written in a form MSVC 6 compiles to different
(though equivalent) instructions, e.g. a 32-bit expression where the original
worked in 16-bit words, or a temporary landing in a different register.

```
make vc6-match                      # coverage summary + per-module tally
make vc6-match ARGS="--file dist"   # per-function detail for one module
make vc6-match ARGS=--diffs         # list every current diff
```

Method: for every `patch_replace(ADDR_*, (const void*)Fn, ...)` target, compile
the owning source at /O2, pull the function's bytes + COFF relocations out of
the `.obj`, and compare to the original with relocated 4-byte slots (addresses
the linker has not filled in) wildcarded and trailing padding trimmed.

MSVC6 compatibility lives in `include/stdint.h` (the C99 types MSVC6 lacks, and,
gated to non-GCC so the real Linux build never sees them: `__attribute__(x)` →
nothing, a for-scope shim `#define for if(0){}else for`, and `__builtin_memcpy`
→ `memcpy`).  Both shims were verified byte-identical to plain `/O2` codegen.
The one source change the host needed was spelling the weak-symbol trace guard
`!(&am2_trace_window)` instead of `!am2_trace_window` (MSVC6 won't apply `!` to
a function designator); GCC compiles both to identical code.

Scope today: the flat `src/game/*.cpp` (pure computation) compile and compare.
`src/game/win32/*` pulls in windows.h/ddraw.h and trips a delay-load gap in the
hosted `c1xx` (`RaiseException 0xc06d007e`) not yet covered by the shim -- those
are reported SKIP.  `crt.cpp`/`trig.cpp` are GCC-only (POSIX, inline `__asm__`).

### What matches piecewise, and what needs the relink

The reconstruction is compiled ONE .obj at a time and compared to the shipped
image, so what can match is bounded by that:

- **Matches now:** leaf functions over their args/stack, and functions that
  touch original globals only as absolute scalars (`mov [0x50c368], ecx`) -- the
  shipped image baked the address in, so our compile-time constant produces the
  identical bytes.  Direct calls to reconstructed siblings match too (the call's
  4-byte target is a relocation, wildcarded).
- **Cannot match until a full relink:** the piecewise reconstruction reaches
  original *code* and *indexed data* by absolute constant address, which MSVC
  folds differently than the original's relocatable symbols did.  Two systematic
  shapes: (1) a call to un-reconstructed code goes through a pointer variable
  (`call [am2_free]`, `ff15`) where the original linked a direct `call rel32`
  (`e8`); (2) an indexed global `g_tbl[i]` behind a `((T*)ADDR)` macro folds the
  base into the index math (`lea ...+base/4` / `[idx*4+0]`) where the original,
  with `g_tbl` a real symbol, emitted `[idx*4 + &g_tbl]`.  Both are *equivalent*
  and *correct*; they differ only because address is constant here, symbol there.
  Closing them means the endgame: compile every TU and LINK with all functions
  and migrated globals as symbols placed at their original VAs -- at which point
  the fold/indirect forms become the original's direct/symbol forms.  This is
  also why finishing the origdata symbol migration feeds the byte-match.
- **Fixable per function now:** genuine source-shape diffs -- a signed vs
  unsigned loop bound (`jl` vs `jb`), a statement order that changes scheduling,
  a 16-bit vs 32-bit expression.  ResetTimers (signed address cursor) was the
  first closed this way.

## Status

- ✅ `CL.EXE` runs and prints its banner.
- ✅ `cl -c foo.c` → a valid i386 COFF `.obj` with the original MSVC6 codegen
  (`c1`/`c1xx` frontend → `c2` backend, `mspdb60` hosted).
- ✅ recompile the flat `src/game` and byte-match: a first tranche of functions
  (ApproxDist, PackKey, Clamp, PointInRect, …) reproduce the shipped bytes
  exactly; run `make vc6-match` for the live count.  The rest are source-shape
  diffs to close one function at a time.
- ☐ `src/game/win32/*` (needs the c1xx delay-load shim gap closed), then
  `LINK.EXE` for a whole-binary match.
