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
| `0x40b800`–`0x41e000` | audio: RIFF/WAVE parsing, DirectSound buffer management |
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
| `0x00427820` | 0 | **3,098** | lookup by ID: a finder at `0x004277A0` returns an index into a 12-byte-record table at `0x00514F0C`, returning the field at `+4` |
| `0x0042DDE0` | — | 369 | `ApproxDist` — reconstructed |
| `0x0040C040` | **0** | 35 | audio |
| `0x0040F560` | — | 23 | audio |
| `0x0041F520` | — | 19 | event |
| `0x00453D50` | 7 | 14 | takes a heap object pointer |
| `0x004540F0` | — | 7 | UI button loader — takes three bitmap names per call (normal/highlight/pressed), e.g. `03_104_00_movies.bmp` |
| `0x0042A7B0` | **0** | 6 | item |

Reaching gameplay needs the game driven; no command-line switch does it alone.

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
