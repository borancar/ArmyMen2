# The byte-match lab: what we are doing and how it is run

`docs/bytematching.md` is the method and the catalogue of source shapes that
worked; `docs/vc6-regalloc.md` is the register-allocation model. This file is
the operating manual: the goal, how a working copy ("lab") is set up, how the
compiler is run, and how a result is judged. Read it before starting a pass.

## What we are trying to do

Make the hosted Visual C++ 6 compiler turn our reconstructed C++ into the
same bytes as the 1999 build of `ArmyMen2.exe`, function by function. The
source is only "reconstructed" if it is something the original author could have
written; a spelling that exists only to nudge the register allocator is a
*number win* and is listed in `docs/numberwins.md` for review after the binary
matches. Every edit must give the same result for all inputs, use fixed-width
types, and add no read-shorthand `#define`s (see `CLAUDE.md`).

Standing decisions for the remaining functions are in `docs/bytematching.md`
(for LoadMap: all seven plane checks are `(int32_t)size != plane`, and in the
SCEN arm `consumed += size` comes before the `ParseScenarios` call).

## The compiler

`build/vc6host` (built by `make vc6host`, source `src/vc6/vc6host.cpp` and
`shim.c`) is our own PE loader plus a Win32/MSVCRT shim. It runs the original
toolchain from `VC6_ROOT` (default `/home/boran/claude/en_vs6_ent`) on Linux
with no Wine: `CL.EXE`, `C1.DLL`/`C1XX.DLL` (front end) and `C2.DLL` (code
generator). All are build 12.00.8168, the same build the game's own objects
carry in the Rich header of `ArmyMen2.exe`.

A translation unit is compiled as:

    build/vc6host $VC6_ROOT/VC98/BIN/CL.EXE /O2 /GX /c /DAM2_STANDALONE /DAM2_RELINK /Fo<obj> <file>.cpp

with `INCLUDE` set to `src/vc6/include` (the shim headers) plus the VC98 headers
(`bytematch.compile_file`). `RELINK_CFLAGS` adds switches. `/GX` is on because
95 original functions carry exception-handling frames.

The host has two probes for generated-code research:

* `VC6_KEEPTMP=1 TMP=DIR` keeps c1xx's intermediate-language files (`ex`, `gl`,
  `in`, `sy`) in DIR.
* `VC6_IL_OVERRIDE=DIR` makes c2 compile DIR's copies instead, so hand-edited IL
  can be tried. A length-changing edit to `ex` must add the delta to the start of
  every later function in `gl`. Opcode notes: `docs/vc6-regalloc.md`.

## The match check

`src/vc6/relink.py` (`make vc6-relink`) simulates the link. It compiles every
VC6-buildable TU, lays each reconstructed function at its real virtual address,
resolves every relocation to the address the linker would have written, and
compares the resolved bytes to the shipped image with no wildcarding of
reconstructed targets. Per function it records:

* `ok`: equal everywhere except bytes that are legitimately unresolved
  (absolute addresses of data the relink does not place); those positions are
  listed in `wild`.
* the **edit distance**: `len(ours) + len(orig) - 2 * (bytes matched by
  difflib)`. A replaced byte costs 2, and a one-byte length difference shifts
  everything after it, so the figure is lumpy and about twice the number of
  differing bytes. Zero means identical. Functions that are `ok` can still show a
  non-zero distance from wildcarded addresses; only the `ok` flag counts.

Headline status is "functions with `ok`" and the summed distance of the others.
Bytes unmatched after alignment (matched blocks against the image's length) is the
fairer size measure; the distance double counts.

## The loop: `tools/relinkscreen.py`

    tools/relinkscreen.py --baseline        full relink; saves build/relink-base.json and
                                            build/relink-syms.pkl (alias --rebase). Run first, and
                                            again after every commit that changes code.
    tools/relinkscreen.py SRC EDITS.py FN.. one-second screen: apply the edits to SRC in place,
                                            compile only that TU with the saved tables, print
                                            `FN old -> new`, restore SRC.
    tools/relinkscreen.py --confirm SRC EDITS.py
                                            same edit through a full relink; prints every function
                                            whose distance changed and the match/differ counts.
                                            Confirm every win here: a screen can miss a cross-unit
                                            effect.
    tools/relinkscreen.py --list            functions with a non-zero distance, largest first.

`EDITS.py` is `E = [(old_text, new_text), ...]`; each old text should occur once
(every occurrence is replaced, so anchor it, or a sibling function sharing the
lines gets the edit too). `make vc6-relink ARGS=--diffs` lists every difference.

A screen is not a build. After a change is adopted run a plain `make`, read the
warnings, and run `make check`; the relink and `make check` skip some sources.

## Reading a difference

* Print our and the image's disassembly side by side with the
  jump-displacement-only hunks removed: capstone over the two byte strings from
  `relink-base.json`, difflib on the instruction text. A displacement that differs
  by one is usually a knock-on of a byte earlier.
* `tools/c2rot.py atr|map|diag|oracle SRC FN` shows C2.DLL's scratch-register
  rotation events and which instruction each moves; `tools/c2webs.py`,
  `c2blk.py`, `c2allow.py` print the colouring tables; `tools/c2walk.py SRC FN
  [--edits E.py] [--lines LO HI]` logs every tuple and operand entry the rotation
  visits and where the cursor moves. All need gdb and a writable `src/` (they compile
  a sibling temp copy), and take `C2ROT_LAB=<root>` to point at a lab.

## How a lab is run

Working directly in the repo is fine for one agent at a time because the screen
restores the file. Parallel work needs copies:

* **Git worktree lab**: `git worktree add --detach LAB HEAD`, then copy `build/`
  (`cp -a build LAB/build`) and symlink `.venv`, `.wine`, `reference` into it.
  `relinkscreen.py` resolves paths from its own location, so a lab's tools and
  source are independent of the main tree. Run `--baseline` once inside the lab,
  or copy the repo's `build/relink-*` after a baseline, before screening.
* **One process per source file**: two screens on the same `.cpp` would
  overwrite each other's in-place edit. Different files in one lab are fine.
* **Subagents** (when allowed): one lab each, told to leave the repo alone,
  report exact old/new edits, and delete the lab. Scratch scripts go under the job
  tmp directory, never under `src/` (a stray `.cpp` there breaks `make check`).
* **Searches in the background**: a mutation search (apply a semantics-preserving
  rewrite, screen, keep it if not worse) is safe only if its rewrites really
  preserve the value. Flipping `cr + cl` inside `cols - cr + cl - 1` does not, and
  produced a false improvement once; check any win by reading the source diff, not
  only the score.
* **Stopping processes**: by PID (`tools/pidfd.py`), never `pkill -f` with a
  pattern that also appears in the killing command line.

## Reporting

After a change: functions byte-identical out of 1,646, bytes unmatched after
alignment out of the image's code size, and the per-function distances. Update
`STATUS.md` and `docs/bytematching.md` with anything learned, including negative
results, and run `tools/checkclaims.py` and `tools/checkprose.py` for the doc edits.
