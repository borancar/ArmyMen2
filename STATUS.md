# Status

A dated snapshot of where the reconstruction is, kept so a session does not
have to re-derive it. **`CLAUDE.md` and `docs/` are authoritative**; this file
is a summary and can be stale between updates. Every number below carries the
command that produces it, so it can be re-measured rather than believed.

Last updated: **2026-08-21**, at `56e30cf`. Working tree clean.

## In flight

Nothing uncommitted.

- **`EvtSetField540`** (`0x0041FAB0`, 48 B, one caller at `0x00420FBE`) --
  committed at `2697730`, and **not verified against the running game**. Sixth
  member of the uid-setter family; gated on `ObjIsType2` alone rather than the
  2/3/8 set, writes `+0x540`. Static checks and the offline selftest pass; the
  A/B has not been run. It is the third commit in a row in that position --
  `e249071` and `6a6b94e` are the other two -- so `tools/ab.sh campaign` is
  owed on all three, not just the last.
- **Stale module counts in `CLAUDE.md`** -- fixed at `56e30cf`. The
  source-layout section said eight flat modules and fourteen boundary; it is 19
  and 15. `tools/checkclaims.py` derives both from `am2.game_sources()` now and
  was tested in both failing directions. Five checked claims became seven.

## Where the work is

The **Win32/DirectX boundary phase is finished** -- `docs/boundary.md` reports
0 outstanding on every channel it can see: named imports, imports by ordinal,
COM vtables, runtime resolution, delay imports. The three `MessageBoxA` sites
left are a decision, not an omission: all three sit behind CD checks this build
has patched to jump past them.

The front has moved into **game logic**. `event.cpp` is the current module --
the registration table, the script conditions, and now savegame serialisation
(`SaveScriptCond` / `LoadScriptCond` / `LoadEventSection` / `LoadScriptConditions`).
The save and load halves mirror each other, which is what is confirming the
condition struct's layout from both ends.

## Measured

| | | how |
|---|---:|---|
| `patch_replace` sites | 295 | `grep -rc patch_replace src/game` |
| distinct addresses reconstructed | 295 | 289 of them below the CRT line |
| sub-CRT functions in the image | 1,239 | `docs/functions.tsv` |
| sub-CRT code reconstructed | 70,000 / 372,816 B (**18.8%**) | patched entries' sizes over the total |
| modules | 19 flat + 15 `win32/` | `tools/checkclaims.py` |
| boundary functions reconstructed | 56, 160 import sites | `docs/boundary.md` |
| COM dispatch outstanding | 0 of 79 functions | `docs/boundary.md` |

Read the percentage as what still crosses an original boundary, not as how
much of the game runs on our code -- the count-of-0 blind spot cuts the other
way, and `tools/blindspots.py` says which counters can move at all.

## Verification state

| check | when | result |
|---|---|---|
| `make` | this session | builds clean |
| `make check` (16 static checks) | this session | all pass, generated files regenerate identically |
| `make selftest` | this session | 6,462 vectors, 15,228 words, 13,956 lines, 9,062 spine, 198 variable -- 0 fail |
| `tools/ab.sh all` | **not this session** | the other half of verification; needs the game |

## Next

1. `tools/ab.sh campaign` for `EvtSetField540`, then commit it.
2. Continue the save/load family in `event.cpp`.
3. Standing: `docs/scriptactions.md`'s oracle, the Lock/Unlock rasteriser
   batch (4 of 29 done), and object types 2, 3 and 8 still unidentified.
