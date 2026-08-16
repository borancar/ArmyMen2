# ArmyMen2.exe — initial reconnaissance

Target: `ArmyMen2.exe` from the GOG release of Army Men II (3DO, 1999),
installed under `.wine/drive_c/GOG Games/Army Men II/`.

## Binary facts

| | |
|---|---|
| Format | PE32, i386, Windows GUI subsystem |
| Linker | MSVC 6.0 (`MajorLinkerVersion 6`) |
| Timestamp | 1999-02-03 04:02:30 |
| Image base | `0x00400000`, **relocations stripped** |
| Entry point | `0x004664c0` |
| `.text` | `0x00401000`–`0x0046eb82` (449,410 bytes) |
| `.rdata` | `0x0046f000`–`0x00472658` |
| `.data` | `0x00473000`–`0x00665c54` (0x1b000 on disk, ~1.9 MB of BSS beyond) |
| `.rsrc` | `0x00666000`–`0x00666888` |

Relocations being stripped is a gift: the image can only ever load at
`0x400000`, so every address in these notes is stable across runs and across
machines. That is what makes call-site patching viable.

## Debug information

**There is no PDB and there never can be one.** The Debug Directory
(data directory entry 6) is `0/0`, and there is no `RSDS` or `NB10` CodeView
record anywhere in the file. A PDB is bound to its binary by the GUID/age in
that record; with no record there is nothing to match against, so no symbol
server lookup is possible even in principle. Symbols and line numbers are
stripped.

## Imports

The Win32 surface is remarkably small, which is good news for the port:

- **DDRAW.dll** — `DirectDrawCreate` only
- **DSOUND.dll** — ordinal 1 (`DirectSoundCreate`)
- **DINPUT.dll** — `DirectInputCreateA`
- **smackw32.dll** — 9 Smacker video functions (`_SmackOpen@12`, `_SmackDoFrame@4`, …)
- **WINMM.dll** — `mmio*` chunk I/O and the `time*` multimedia timers
- **ole32.dll** — `CoInitialize` / `CoCreateInstance` (this is how DirectPlay is obtained)
- KERNEL32 / USER32 / GDI32 / ADVAPI32 — ordinary, plus a statically linked MSVC 6 CRT

GOG ships a wrapper `ddraw.dll` alongside the game (1.2 MB, contains libpng,
libjpeg and shader-model strings) which re-implements DirectDraw over a modern
backend. We do not need it; Wine's own `ddraw` is the baseline.

## Embedded source filenames

`.data` contains ten paths of the form `C:\ArmyMen2\source\<name>.cpp`:

    air.cpp  audio.cpp  event.cpp  gameproc.cpp  item.cpp
    map.cpp  objscript.cpp  pad.cpp  script.cpp  unit.cpp

These are **not** `assert()` sites. They are arguments to a savegame chunk-tag
verifier — see below.

## `CheckSaveTag` at `0x004235d0`

Reconstructed signature:

```c
BOOL CheckSaveTag(FILE *fp, uint32_t expected, const char *file, int32_t line);
```

It `fread`s four bytes (reading over the `fp` argument slot), compares them to
`expected`, and on mismatch calls the logger with
`"Error reading save file, source file: %s  line: %d\n"`. Returns 1 on match.

Supporting identifications:
- `0x004645c1` = `fread(void*, size_t, size_t, FILE*)` — `imul edi,[ebp+0x10]` is `size*count`
- `0x0045caa0` = the debug logger, **stubbed to a bare `ret`** in the retail build

The 15 call sites yield the savegame section tags. Full table in
[`savetags.tsv`](savetags.tsv):

| tag | section |
|---|---|
| `0x06660002` | script.cpp |
| `0x06660003`, `0x06660004`, `0x06660006`, `0x00003e88` | event.cpp |
| `0x06660005` | pad.cpp |
| `0x06660007` | item.cpp |
| `0x06660008` | objscript.cpp |
| `0x06660009` | map.cpp |
| `0x06660010` | air.cpp |
| `0x06660666` (×2) | gameproc.cpp |
| `0x06660668` | unit.cpp |
| `0x01326413` | audio.cpp |

One site at `0x004268ff` passes its arguments in registers, so the static
extractor cannot resolve it. Runtime tracing during a savegame load does:
it is **`gameproc.cpp:2253`, tag `0x00000438`**, called twice per load.

Loading `map0_mission0.sav` hits all 15 sites and confirms every
`(file, line, tag)` triple above exactly. Static and dynamic analysis each
cover the other's blind spot — worth remembering for the rest of the project.

## Link order is alphabetical

The `(address, filename)` anchors are strictly monotonic in alphabetical
filename order. The linker consumed the `.obj` files alphabetically, so any
function's address bounds it between two known filenames. This is what
`tools/functions.py` uses to attribute translation units.

Caveat: the first object linked is a DirectPlay comm module whose messages
start at `0x00401000`, i.e. it sorts *before* `air.cpp`. Its real filename is
still unknown.

## Function inventory

`tools/functions.py` finds **1,584 functions** covering 100% of `.text`, from
1,578 direct `call rel32` targets plus the entry point and 5 isolated
prologues. See [`functions.tsv`](functions.tsv).

The cluster around `0x00464xxx` (`fread` at `0x004645c1` with 192 callers, plus
several tiny 11–47 byte functions with 100–253 callers each) is the statically
linked MSVC 6 CRT. Roughly `0x0045c000`–`0x0046eb82` is runtime, not game code,
and does **not** need reconstructing — we link libc instead. Actual game code is
approximately `0x00401000`–`0x0045c000`.

## Recovered debug messages

Because the logger was stubbed at the *callee* rather than at the call sites,
the calls still push their real format strings. `tools/find_logs.py` recovers
**599** of them across 216 functions — see [`logs.tsv`](logs.tsv).

> **Correction.** An earlier version of this document treated all 623 callers of
> `0x0045CAA0` as calls to one logger. That is wrong. MSVC 6 folds identical
> COMDATs (`/OPT:ICF`), and *every* function whose body compiled down to a bare
> `ret` merges to the same address. So `0x0045CAA0` is the shared address of
> several distinct stubbed-out functions with different signatures, and the
> caller count conflates them.
>
> This was found by running: with the logger un-stubbed, some call sites pass a
> "format string" that is really a code address (`0x004254DC`, `0x00403507`) or a
> small integer. Treat any single folded address as a set of functions until
> proven otherwise — the same caveat applies to every other tiny function in
> `functions.tsv`, where a 617-caller count may be several functions in a trench
> coat.

This is the densest naming source in the binary. Original function names
visible in the messages include `AddMsg`, `RemHead`, `RemMsg`, `ReadBitmap`,
`CreateBitmapSurface`, `PauseGame`, `UnPauseGame`, `EventTriggerImmediate`,
`EventTriggerDelayed`, `EventMessageSend`, `EventMessageReceive`,
`ScriptResurrectItem`, `ScriptSetObjBitmap`, `ChangeObjectFrame`,
`TriggerItemDestroyed`, `FreeItem`, `DeployItem`, `AddToItemList`,
`DestroyItemObject`, `itemGoneMessageSend`, `itemDeployMessageSend`,
`AddNameTableName`, `DefObjParse`, `DefLinkParse`, `ExitAllFromVehicle`,
`vehicleUpdateMessageAppend`, `vehicleUpdateMessageUnpack`.

### Subsystem map inferred from message clustering

| `.text` range | subsystem |
|---|---|
| `0x401000`–`0x409000` | DirectPlay reliable-messaging layer (flow control, ACK/NACK, resend queues) |
| `0x409000`–`0x40b800` | session / player management (`air.cpp`) |
| `0x40b800`–`0x414000` | audio: RIFF/WAVE parsing, DirectSound buffer management |
| `0x414000`–`0x41e000` | 2D drawing — mislabelled as audio in the first pass; the functions here call `ClipRect`, `PointInRect` and `RectSet`, and `0x0041C710` is the pixel blitter |
| `0x41e000`–`0x423000` | events, script-driven object commands |
| `0x423000`–`0x425000` | bitmap loading, DirectDraw surface creation, object data files |
| `0x425000`–`0x428000` | game loop, pause, timing (`QueryPerformanceCounter`) |
| `0x428000`–`0x42d000` | items: deploy, damage, destroy, network replication |
| `0x42d000`–`0x436000` | map, checksums, `object.aai` parsing |
| `0x436000`–`0x43f000` | object-script tokenizer/parser |
| `0x43f000`–`0x458000` | script system, name tables (206 messages — the largest band) |
| `0x458000`–`0x45c000` | units and vehicles |
| `0x45c000`–`0x46eb82` | MSVC 6 CRT |

## Developer command-line switches

The retail build still parses its development switches. All eleven are handled
in one contiguous chain at `0x0040b37d`–`0x0040b4a9`:

| switch | referenced at |
|---|---|
| `-nointro` | `0x0040b37d` |
| `-tracePF` | `0x0040b3c0` |
| `-traceVEH` | `0x0040b3d8` |
| `-debugComm` | `0x0040b3f0` |
| `-traceComm` | `0x0040b40d` |
| `-logComm` | `0x0040b42b` |
| `-tracewin` | `0x0040b449` |
| `-dbg` | `0x0040b461` |
| `-rob` | `0x0040b479` |
| `-peter` | `0x0040b491` |
| `-dan` | `0x0040b4a9` |

`-rob`, `-peter` and `-dan` are per-developer modes. `-nointro -dbg` is the
useful pair for iteration: it skips the intro and drives the startup path
substantially further, which is how the hot-function survey below was obtained.

## Hot functions on the startup path

Measured with the observation harness over a 45-second run to the title screen,
using `-nointro -dbg` (see `docs/01-harness.md`):

| function | calls | note |
|---|---|---|
| `0x0042E1C0` | 90,185 | `RectSet` — reconstructed |
| `0x00422DE0` | 10 | data-path builder, `<basepath>\<name>` from the global at `0x51235C` |
| `0x00453D50` | 7 | operates on heap objects |
| `0x0040C040` | 0 | audio — not reached before gameplay |
| `0x00427820` | 0 | not reached |
| `0x0042A7B0` | 0 | not reached |

The distribution is the useful part: reaching the title screen exercises almost
none of the engine.

### The same survey, in gameplay

Driven into Boot Camp through the control socket, every one of the candidates
runs — including the three that were completely dead at the title screen:

| function | title | gameplay | identification |
|---|---|---|---|
| `0x00427820` | 0 | **3,098** | `LookupByUID` — reconstructed; see the object registry below |
| `0x0042DDE0` | — | 369 | `ApproxDist` — reconstructed |
| `0x0040C040` | **0** | 35 | audio |
| `0x0040F560` | — | 23 | audio |
| `0x0041F520` | — | 19 | event |
| `0x00453D50` | 7 | 14 | takes a heap object pointer |
| `0x004540F0` | — | 7 | UI button loader — takes three bitmap names per call (normal/highlight/pressed), e.g. `03_104_00_movies.bmp` |
| `0x0042A7B0` | **0** | 6 | item |

Reaching gameplay needs the game driven; no command-line switch does it alone.

## The object type taxonomy

The type at `+0x00` is dispatched through a 9-entry jump table in
`AddToItemList`, so valid types are 0..8. Four predicates over it, together
accounting for 148 call sites, narrow the meaning down:

| address | tests | calls in one Boot Camp session |
|---|---|---|
| `0x00433860` | types 1, 4 | 750,457 |
| `0x00457470` | type 2 | 501,957 |
| `0x00457490` | type 3 | 1,840 |
| `0x00457420` | types 2, 3, 8 | 62,160,244 |

What is established:

- Types **1 and 4 are items**. Evidenced, not inferred: the functions guarded by
  that predicate report *"ScriptSetObjBitmap was called with %s which is not an
  item"* and *"SetObjScriptState was called with %s which is not an item"* when
  it fails.
- Types **1, 2, 3, 4 and 8 are army-owned** — they carry their own owner byte at
  `+0x10` — while **0, 5, 6 and 7 are not**, taking the owner from the global at
  `0x004F9FDC`.
- Types **2, 3 and 8 are therefore the owned non-item types**, and `0x00457420`
  tests exactly that set. They live in `unit.cpp`, immediately after its
  savegame anchor at `0x0045734D`.

What is **not** established is what 2, 3 and 8 individually mean. The obvious
guess given `troop.aai` and `vehicle.aai` is troop and vehicle, and the call
counts are consistent with it — Boot Camp fields infantry and no vehicles, and
type 2 is tested 273 times more often than type 3. But that is circumstantial:
the ratio could equally reflect where the predicates are called from rather than
how many objects of each type exist. The AAI loader passes only filenames and no
type constants, so nothing pins the mapping down yet, and these are named
structurally (`ObjIsType2`, `ObjIsType3`) until something does.

## Geometry primitives, and how the types were pinned down

Three functions sit directly beneath the drawing code and are among the busiest
in the engine:

| address | function | calls per Boot Camp session |
|---|---|---|
| `0x0042E1C0` | `RectSet` | 285,554 |
| `0x0042E180` | `Clamp` | 547,390 |
| `0x0042E1F0` | `PointInRect` | 15,473,860 |

Their real value is corroboration. `AM2_Rect` and `AM2_Point` were each first
inferred from a single function, which is weak evidence. `PointInRect` then
independently confirmed both at once: it reads four dwords at `+0/+4/+8/+0x0C`
and compares the point's x against the first and third and its y against the
second and fourth — exactly the four edges `RectSet` writes, indexed against
exactly the two `int16` coordinates `ApproxDist` reads. Two separately guessed
layouts, confirmed by a third function that has to agree with both.

It also settles a detail neither of the others could: the right and bottom
comparisons use `jge`, so those edges are exclusive.

The functions *calling* these are large composites — `0x00430530` is 4,512
bytes with 20 `RectSet` call sites alone — so the drawing chain is best climbed
from the primitives upward rather than the other way round.

### The blit clipper

`ClipRect` (`0x0042E220`, 5,146,540 calls per session) is the piece that makes
the drawing composites tractable. Given a source rectangle, a clip rectangle and
a destination position passed by pointer, it moves the destination corner out to
the clip edge where the clip cuts in, and records how far into the source that
landed.

The important detail is that its output rectangle is in **source** space, not
destination space: `out->left` and `out->top` are offsets into the bitmap, which
is exactly what a blit needs in order to skip the clipped-away columns and rows.
Early rejects leave `out` only partly written, so the return value has to be
checked rather than inferring emptiness from the rectangle.

### DrawText: the first composite handled

`DrawText` (`0x00446930`, 384 bytes, 34 call sites) is the first substantial
drawing routine reconstructed rather than a primitive, and it only became
tractable because everything under it — `ClipRect`, `AM2_Rect` — was done first.

Its argument order was **measured, not derived**. The stack frame is
`sub esp,0x2C` plus four pushes, with arguments re-read at shifting offsets and
argument slots reused as scratch, so deriving it on paper invites getting it
backwards. Observing it instead settled it immediately: the HUD stat panel
produces `DrawText(0x22C, 0x111, "Sarge", 0, 0, 0xFE)` and
`DrawText(0x240, 0x11D, "4.6 cm", 0, 0, 0xFE)` — labels at x=556, values at
x=576, y stepping 12 a row, exactly the panel on screen.

Glyph lookup uses two tables with awkward strides:

```
offset = glyphOffsets[(int8_t)ch + font * 262]   uint16, 0x006598D4
base   = fontBases[font * 133]                   uint8_t *, 0x00659AD4
width  = *(uint16 *)(base + offset + 0)
height = *(uint16 *)(base + offset + 2)
```

The character index is sign-extended, so bytes above `0x7F` index backwards from
the font base.

Two behaviours worth knowing:

- A `^` in the string is a **colour escape** — the next character replaces the
  colour argument for the rest of the string, and both are consumed.
- A glyph that clips away entirely does not get skipped, it **ends the string**:
  the original jumps to the epilogue. Text running off the right edge therefore
  truncates, which is sensible; text starting off the left edge draws nothing at
  all, which probably is not intended.

The blitter beneath it, `0x0041C710`, is **`__fastcall`** — destination x and y
arrive in `ecx`/`edx` with the clipped rectangle passed by value. That is
invisible to the argument tracer, which only reads stack dwords, and is worth
remembering: a traced argument list that starts at what looks like the second or
third parameter is a sign of a register calling convention.

### BlitGlyph and the glyph format

`BlitGlyph` (`0x0041C710`, 874,768 calls a session) completes the text path.
Each glyph row is a stream of alternating byte counts — pixels to leave alone,
then pixels to fill — and the runs carry **coverage only, never colour**: the
fill uses the caller's colour byte via `rep stosb`.

That single fact explains the design above it. One font can be drawn in any
palette entry, which is exactly what `DrawText`'s `^` colour escape needs, and
it is why glyph data is so compact.

```
+0            uint16   width
+2            uint16   height
+4            uint16   rowOffset[height]   byte offset of each row's stream
rowOffset[r]  uint8    alternating skip and run lengths
```

Horizontal clipping runs against `src.left`/`src.right` in glyph space while the
destination pointer advances in step, so the pointer effectively starts at the
pixel for `src.left`. Vertical clipping is simply which rows get walked. The
framebuffer is 8-bit paletted, base at `0x004FE1A8`, pitch at `0x00502AD0` —
which sits immediately below `ORIGIN_SEL_B`, so these are probably fields of one
screen descriptor rather than loose globals.

### The packed map key

A 26-bit key built by `PackKey` (`0x00433810`) and taken apart by three readers:

| bits | field | reader |
|---|---|---|
| 25..19 | A, 7 bits | `0x00433830` |
| 18..17 | *unused* | — |
| 16..7 | B, 10 bits | `0x00433840` |
| 6..0 | C, 7 bits | `0x00433850` |

The two-bit gap is real. `PackKey` computes `((a << 12) + b) << 7) + c`, leaving
room for twelve bits of B, but every reader masks only ten. So either callers
guarantee B < 1024, or larger values are silently truncated on the way back
out — a latent limit that would only ever show up on a large map.

What the fields mean is not established. Seven bits is 0..127, which would suit a
tile coordinate, but that is a shape argument rather than evidence, so they are
named structurally.

## The global object registry

The first real data structure recovered. Every gameplay subsystem addresses
objects by a 32-bit UID — the recovered debug messages are full of `uid=%x` for
items, units, vehicles and events — and this table turns one back into a
pointer. It is by a wide margin the hottest thing in the engine.

Records are 12 bytes, identified by the `lea r,[i+i*2]` then `*4` addressing
used at every access site:

| offset | type | meaning |
|---|---|---|
| `+0` | `uint32_t` | uid — the search key, kept sorted ascending |
| `+4` | `void *` | the object; may be NULL |
| `+8` | `uint32_t` | iteration stamp, from the global at `0x0051308C` |

That third field looks like a serial number and is not one. It is how iteration
stays correct while the table is being mutated: `FirstItem` (`0x00427850`)
resets a cursor at `0x00514F08` and bumps the stamp, and `NextItem`
(`0x00427880`) skips any entry already carrying the current stamp, marking each
one it returns. Since an insert memmoves the tail, an index held across a walk
can be invalidated — so the table does not rely on index stability at all, and
marks entries instead. `AddToItemList` writes 0 rather than the current stamp,
so an object registered during a walk still gets visited by it.

Supporting globals, each confirmed by a second independent use:

| address | meaning | confirmed by |
|---|---|---|
| `0x00514F0C` | table base | nulled with the count on reset at `0x0042949E` |
| `0x00514F04` | entry count | zeroed on the same reset |
| `0x00514F00` | capacity | grown in steps of 100 at `0x00429879` |

`FindSlot` (`0x004277A0`) is a binary search returning the index, or `-1` with
the insertion position written through an out-param, so callers that are
inserting need not search twice. It has three distinct not-found exits
depending on where the search landed. UIDs are compared with unsigned branches,
so the key is unsigned even though the index arithmetic is signed.

### UID encoding

A UID is not opaque. `AddToItemList` (`0x00429740`) builds it as:

```
uid = (owner << 29) | counter        /* 3 owner bits, 29 counter bits */
```

Established three independent ways in that function: `and eax,7` on the owner,
`shl …,0x1D`, and `and edi,0x1FFFFFFF` when masking a supplied UID back to its
counter. The per-owner counters live in an eight-entry array at `0x00511DE0`,
start at 1000 (`0x3E8`), and overflow is checked against `0x1FFFFC18`, which is
exactly 2²⁹ − 1000. That start value is why the first UIDs observed in traces
were `0x3E8` and `0x3E9`.

Which field supplies the owner depends on the object's type, dispatched through
a 9-entry jump table at `0x0042991C`: types 0, 5, 6 and 7 take it from the
global at `0x004F9FDC`, everything else from the object's own byte at `+0x10`.

That also pins down three fields of the object structure itself:

| offset | type | meaning |
|---|---|---|
| `+0x00` | `uint32_t` | type — the jump-table selector, 0..8 |
| `+0x04` | `uint32_t` | uid, written back on registration |
| `+0x10` | `int8_t` | owner index |

The overflow path contains a defect, reproduced rather than fixed in
`src/game/objtable.c`: it probes for a free UID in one register but the insert
that follows uses another, so the search only advances the counter and its
result is discarded. Reaching it needs 2²⁹ objects for a single owner, so it
was presumably never executed — the same pattern as the stubbed logger, where
code that never ran was quietly wrong.

`LookupByUID` (`0x00427820`) wraps it, and passes the address of its *own*
argument slot as the out-param — one stack dword serving as both input and
output. That is safe because `FindSlot` reads the key into a register before it
ever writes through the pointer. `CheckSaveTag` uses the same trick with its
`FILE *` argument, so it appears to be a habit of this codebase rather than a
one-off.

## Toolchain constraints

Wine 11 on this host is **new-WoW64**: `/usr/lib64/wine` contains
`i386-windows` (PE builtins) and `x86_64-unix`, but **no `i386-unix`**.
32-bit winelib ELF objects therefore cannot be built here. Consequences:

- The iterative function-replacement stage must use a **32-bit PE DLL** built
  with `i686-w64-mingw32-g++`, injected into the game's address space.
- winelib only becomes an option later, at 64-bit, once the reconstruction is
  standalone enough to stop hosting the original binary.

Installed and verified: `i686-w64-mingw32-gcc/g++` 16.1.1, `radare2` 5.9.8,
`winegcc`/`winebuild`, `Xvfb`. Python analysis runs from `.venv`
(`pefile` 2024.8.26, `capstone` 5.0.7).

## Baseline

The game launches and runs under Wine in a virtual desktop:

```
WINEPREFIX=$PWD/.wine wine explorer /desktop=amii,800x600 \
    "C:\GOG Games\Army Men II\ArmyMen2.exe"
```
