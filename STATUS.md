# Status

A dated snapshot of where the reconstruction is, kept so a session does not
have to re-derive it. **`CLAUDE.md` and `docs/` are authoritative**; this file
is a summary and can be stale between updates. Every number below carries the
command that produces it, so it can be re-measured rather than believed.

Last updated: **2026-08-21**, at `3430e9e`. Working tree clean.

## In flight

Nothing uncommitted.

- **A whole setter family is reconstructed and never runs.** A counts probe on
  the campaign reads `EvtSetField540=0`, `EvtSetByte530=0` and
  `LookupType3ByUID=0`; of the family only `EvtSetByte40` fires, 4 times. They
  are arms of the action executor at `0x00420410`, so which ones run is decided
  by which actions the shipped scripts use, and `kitchen1.txt` uses almost none
  of them. **The clean campaign A/B says nothing about any of them** -- it
  establishes only that nothing else broke. Verified by reading.
  `blindspots.py` confirms these counters can move, so the zeroes are real
  rather than the usual blind spot.
- **The pure-leaf pool is nearly empty**: 14 pure unreconstructed leaves left,
  from the 161 this project started that count at. Three of the 14 are false
  positives -- see Leads.
- **Six holes closed in the vector harness, all found by mutation.** The
  newest is a lockstep: a seeded field and an argument set of the SAME list
  length move together, so `CommRemoveKeyed` never met the record that
  exercises its clamp. Co-prime lengths, the lesson `vectors.py` already
  carried for SEED periods. The
  newest: a `u32v` seed varies on a period fixed by its position in the chain,
  so a table with answers at BOTH ends could not be aimed at -- `ObjCodeUnmapped`
  hit 100% coverage with two of its five zero answers never produced. `u32s`
  takes an explicit value set, as `ARG_VALUES` already does for arguments. The
  correlation heuristics -- copy one argument to another, set one to -1 -- were
  overwriting explicit `ARG_VALUES` sets, so `MaskPixelSolid32` reached only 4
  of 55 (x, y) combinations and could not tell `acc >= x` from `acc > x`. Every
  pointer argument took its NULL from one shared decision, so they were null
  together and never one at a time -- which made `RemapBytes`' whole copy path
  unreachable. And the scratch fill had period 256 while `PTR_STRIDE` is
  `0x800`, so **every pointer argument's region held identical bytes** and a
  copy from src to dst changed nothing observable; every copy-like function in
  the set had been checked against indistinguishable buffers. Both fixed at
  `bbefc4e`, and no existing reconstruction was relying on either.
- **The vectors now check for writes the original did NOT make.** They only
  ever asked whether the expected writes happened, so a reconstruction that
  scribbled elsewhere passed. Found by a `ListUnlink` mutation that should have
  failed and did not. Closing it immediately found a real defect:
  `SetFacing14` and `SetFacing08` set a dword on facing 1 that the original
  sets only on facing 3, and had done since they were written. Fixed at
  `1b6d541`.

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
| `patch_replace` sites | 305 | `grep -rc patch_replace src/game` |
| distinct addresses reconstructed | 305 | 298 of them below the CRT line |
| sub-CRT functions in the image | 1,239 | `docs/functions.tsv` |
| sub-CRT code reconstructed | 70,160 / 372,816 B (**18.8%**) | patched entries' sizes over the total |
| modules | 19 flat + 15 `win32/` | `tools/checkclaims.py` |
| pure unreconstructed leaves | 8 | `tools/vectors.py --all` |
| boundary functions reconstructed | 56, 160 import sites | `docs/boundary.md` |
| COM dispatch outstanding | 0 of 79 functions | `docs/boundary.md` |

Read the percentage as what still crosses an original boundary, not as how
much of the game runs on our code -- the count-of-0 blind spot cuts the other
way, and `tools/blindspots.py` says which counters can move at all.

## Verification state

| check | when | result |
|---|---|---|
| `make` | current | builds clean |
| `make check` (16 static checks) | current | all pass, generated files regenerate identically |
| `make selftest` | current | **6,982** vectors, 15,228 words, 13,956 lines, 9,062 spine, 198 variable -- 0 fail |
| `tools/ab.sh campaign` | current | clean, three times: log identical at 14 messages, 2,571/786,432 pixels every time |
| `tools/ab.sh bootcamp\|windowed\|intro\|audio\|mission\|quit` | not since this run began | the rest of `ab.sh all` is still owed |

A clean A/B is not evidence about a function the run never calls. Check with a
counts probe before reading one as coverage -- that is what turned the
`EvtSetByte530` result from "verified" into "verified by reading", above.

## Next

1. Finish `tools/ab.sh all` -- only `campaign` has been run against current HEAD.
2. Keep taking pure leaves: 5 real ones left -- `0x00439cc0`, `0x0040d880`,
   `0x0041ade0`, `0x00423ee0` and one more. All 160B or larger.
   `0x0040d880` reads `+0x538` and `+0x53c`, the same fields
   `ObjKind538In10To17` and `Field53C` already touch, and dispatches on codes
   0..0x24 through a byte index table -- the third such table in this object.
3. The action executor at `0x00420410` (4,096 B) is the layer above the setter
   family, and `0x0041FD10`, `0x0041FD30`, `0x0041FEA0` are three more of its
   helpers, all still original.

## Leads

Things believed but not established. Written down so they are not re-derived,
and not promoted to fact without evidence.

- **Type 3 may be vehicles.** `0x0045A9C0`, the type-3 arm of the per-type
  teardown at `0x00428DA0`, sits inside a band that is entirely vehicle code --
  `vehicle mask direction`, `ExitAllFromVehicle`, `Vehicle aai entry not found`.
  That is adjacency, not proof, and the check that would settle it came back
  empty: none of `ObjIsType3`'s 25 caller functions carries a log string at all.
  Types 2, 3 and 8 stay unidentified.
- **Three of `vectors.py`'s 14 pure leaves are not targets.** `0x00427974` is
  the jump table `0x004278E0` dispatches through, not a function -- the split
  list is a lower bound and says so. `0x0045CAA0` is the stubbed logger, which
  IS reconstructed, by `src/inject/gamelog.c`; the scan reads the reconstructed
  set from `patch_replace` calls in `src/game` only and cannot see a harness
  patch. Worth fixing in `merges.reconstructed()`, which is where the same
  lesson is already written down.
- **Some things cannot be checked offline at all, and `MaskPixelSolid32` is
  the clean example.** The only difference between it and `MaskPixelSolid` is
  that its row table holds dword offsets rather than word ones -- and a row
  offset must land inside a 0x8000-byte scratch to be followed, so its high
  word is always zero and the two reads are indistinguishable. Mutating the
  dword read back to a word passes every vector. Raising `SCRATCH_SZ` past
  64K would fix it and would re-cut every vector in the set; not done, and
  recorded here rather than left as a silent gap.
- **A seed that is "valid" can still be untestable.** `ObjMaskBitAt` computes
  `mask.origin - obj.pos + point`, and the first seed put the object at (0, 0)
  -- where subtracting and adding are the same expression. The sign mutation
  passed all 6982 vectors at 100% coverage. Ask of every seeded field whether
  its value makes the operation on it observable, not merely legal.
- **The comm object's flow control is coming into focus.** `0x004014C0` is the
  ack handler -- `"??? PULSE seq %d latency %d acks for %d msgs %d thru %d"`,
  `"Flow Ack for Message not in sendqueue sequence %d"` -- and it is the only
  caller of both `RingPush32` and `CommRemoveKeyed`. So the 32-dword ring at
  `+0x3A0` that `CommMean32` averages is a **latency** average, which `msgslot.h`
  had guessed from its shape before the writer was found; and the 12-byte
  records at `+0xBC` are very probably the send queue, keyed by sequence number.
  The second reading is the caller's vocabulary, not the function's, so it is
  recorded and not in a name.
- **Object codes 0x18..0x28 are a real vocabulary, and two tables agree on
  which five matter.** `MapCode18To28` maps `{0x18,0x19,0x1A,0x27,0x28}` to
  `{8,2,1,4,6}` and everything between to 0; `ObjCodeUnmapped` answers 0 for
  exactly those five. Two independently transcribed tables in different parts
  of the image singling out the same subset is worth more than either alone,
  and it is a thread to pull on for what the codes mean.
- **100% instruction coverage is not verification, and there are now two
  worked examples.** `RemapBytes` reached every instruction while `count & 3` and
  `count & 7` were indistinguishable, because no count that reached the copy
  path had bit 2 set. Coverage says which lines ran, never whether the values
  that ran them could tell two behaviours apart. Mutation is what says that.
- **Coverage percentages in `--validate` are not being read.** `SetFacing14`
  and `SetFacing08` have sat at **39.3%** for as long as they have existed and
  the line printing it went past every time. The defect found at `1b6d541` was
  in an arm those vectors do reach, so coverage was not the thing that hid it
  -- but a third of two functions is still unvisited and nothing is tracking
  which functions are short.
