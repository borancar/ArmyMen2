# The reconstruction harness

The harness keeps the original `ArmyMen2.exe` running while we replace its
functions one at a time. Nothing on disk is modified: `am2hook.dll` and
`launcher.exe` are added beside the game, no shipped file is overwritten, and
not launching through `launcher.exe` gives a completely stock install.

## How injection works

`launcher.exe` creates the game with `CREATE_SUSPENDED`, writes the path of
`am2hook.dll` into the child with `VirtualAllocEx`/`WriteProcessMemory`, calls
`LoadLibraryA` there via `CreateRemoteThread`, waits for it to return, and only
then resumes the main thread. The harness therefore installs its patches before
a single instruction of the game's entry point has run.

`kernel32.dll` maps at the same address in every 32-bit process, so our
`LoadLibraryA` is also the child's. Wine implements all of these calls.

## Patching

`patch_replace()` overwrites the first five bytes of a target with
`jmp rel32` to our implementation. This is deliberately **one-way**: the
original body becomes unreachable and there is no trampoline back into it.
That is what lets the harness avoid a length-disassembler entirely.

Before any address is added as a target, validate it offline:

```sh
./.venv/bin/python tools/checkdetour.py 0x004235d0
```

That rejects bodies shorter than five bytes and, critically, any target where
some jump or call lands *inside* the five bytes being overwritten. A split
instruction at `+5` is fine here precisely because the original body is never
re-entered.

## Fingerprinting

`am2hook` refuses to patch anything unless the image base is `0x400000` and the
bytes at every known address match what was disassembled. A different build of
the game would have everything at different addresses, and blind patching would
corrupt it silently. The check costs nothing and turns a mystery crash into a
clear log line.

## Tracing

Reconstructed functions contain **no tracing code**. Under `AM2_TRACE=1`,
`patch_replace()` interposes a stub generated at runtime, which logs the
arguments and then tail-jumps to our implementation:

```
    9C              pushfd
    60              pushad
    8D 44 24 28     lea   eax, [esp+40]     ; &args
    50              push  eax
    68 <id>         push  id
    E8 <rel>        call  trace_enter
    83 C4 08        add   esp, 8
    61              popad
    9D              popfd
    E9 <rel>        jmp   <replacement>     ; stack untouched
```

Twenty-seven bytes, and one shape covers every signature — because the target
is 32-bit cdecl, every argument is a stack dword, so the stub only needs to know
how many to read. `pushfd`/`pushad` mean the traced function cannot tell it is
being observed. And because the stub jumps *forward* into our code rather than
back into patched bytes, no trampoline is needed.

Any argument that points at a short printable string is rendered as text rather
than hex, with control characters escaped so one record stays on one line.

### When we will need a real hooking library

The one thing this design cannot do is **call the original**. That is wanted for
A/B differential testing — run ours and theirs on identical inputs and compare —
which needs a trampoline, which needs a length-disassembler. At that point
vendoring MinHook (or funchook/PolyHook) is the sensible move rather than
writing one. Until then the dependency would buy nothing.

### Tracing the Win32 boundary

For calls *out* of the game, Wine already does this better than we can:

```sh
WINEDEBUG=+relay,+ddraw,+dsound WINEPREFIX=$PWD/.wine wine ...
```

`+relay` logs every cross-DLL call with arguments. It is blind to the game's
internal calls — those are intra-module and never relayed — which is exactly the
gap the stub above fills.

## Observing without replacing

`patch_replace()` is one-way, which is useless for learning what a function we
have *not* reconstructed actually does. `observe_install()` inverts the trick:
it leaves the function byte-for-byte intact and rewrites the rel32 of each
`call target` so it reaches a logging stub, which then jumps on to the real
function. No trampoline, because the original is never modified.

Site lists are resolved offline and exactly by `tools/callsites.py`, not by
scanning for `E8` bytes at runtime — `E8` occurs constantly inside longer
instructions and in data, and a false positive would corrupt the image. Each
site is then re-verified against the live image before being written.

```sh
./.venv/bin/python tools/callsites.py 0x0042e1c0 > src/inject/sites.h
AM2_OBSERVE=1 make run
```

Only direct calls are covered; function-pointer and vtable dispatch still
reaches the original unseen.

Two limitations worth knowing. Argument counts are guesses — reading extra
dwords only walks into the caller's frame, so an over-estimate is noise rather
than a fault, but a wrong count produces misleading records. And the string
heuristic renders *any* argument that points at printable bytes as text, which
misfires badly on out-parameters: observing `RectSet` produced entries like
`("W", ...)` and `(",", ...)` purely because the destination buffer happened to
hold those bytes.

There is also no count-only mode yet. Observing one hot function produced 90,185
records in a 45-second run, which is fine for a survey and much too heavy to
leave enabled.

## Running several instances at once

Concurrent runs collide on four things — the Wine desktop name, the control
port, the log file and the screenshot directory. All four derive from `ID`,
which defaults from `$DISPLAY` (`:99` → 99, `:0` → 0), so a headless run and a
desktop run are independent without anyone having to think about it:

```sh
make run                      # ID from DISPLAY
make run ID=7                 # a second instance on the same display
make config ID=7              # what would that instance be called?
```

`tools/drive.sh` sources those values from `make config` rather than
re-deriving them, so the two cannot drift apart.

### The game refuses to run twice

Separating ports and desktops is not enough. The game guards itself with a
named mutex — `OpenMutexA(…, "ArmyMenMutex")` at `0x0040B603`, then
`CreateMutexA` at `0x0040B62E`. Named kernel objects belong to the wineserver,
and there is one wineserver per `WINEPREFIX`, so a second instance in the same
prefix always loses. It exits immediately after `system speed:`, before
creating any DirectInput device — a quiet, easily misread failure.

`ISOLATE=1` gives an instance its own prefix, hence its own wineserver, hence
its own mutex namespace. The 579 MB install is symlinked rather than copied:

```sh
make run ID=7 ISOLATE=1
```

Caveat: because the install is shared through that symlink, the files the game
*writes* — `Options.cfg` and `save/` — are still shared. Independent game state
needs a real copy.

### Install by rename, never by overwrite

`install-hook` writes to a temporary name and `mv`s into place. This is not
tidiness. Wine maps `am2hook.dll` directly from that file, and `cp` truncates
and rewrites in place — so installing while another instance was running
corrupted *that* instance's mapping, taking its control-socket thread down with
it. Because isolated prefixes symlink to the same directory, simply starting a
second instance did this to the first: it answered one command, then went
silent forever. `rename(2)` swaps the directory entry and leaves the old inode
alive for anyone still mapping it.

### A listening port does not mean a live game

When the game exits, its wineserver keeps the listening socket open. `connect()`
therefore still succeeds against a dead game, and the failure shows up as a read
timeout rather than a refused connection. `am2ctl.py` says so explicitly instead
of reporting a bare timeout. Check `pgrep -f 'ArmyMen2[.]exe'` for liveness, not
the port.

## Driving the game: DirectInput interception

Reaching gameplay means getting past menus, and synthesising X11 input does not
work here. Xvfb has no window manager, so there is no foreground window, and
DirectInput silently discards mouse input as a result. Keyboard happens to get
through, which is worse than an outright failure — it looks like the approach
works until it doesn't.

So input is injected below DirectInput instead. The game imports exactly one
entry point, `DirectInputCreateA`, so one IAT patch at `0x0046F014` is enough to
reach everything else. From there we patch vtable slots rather than writing
wrapper objects — three methods matter against eighteen that would otherwise
need forwarding by hand:

| interface | slot | method |
|---|---|---|
| `IDirectInputA` | 3 | `CreateDevice` |
| `IDirectInputDeviceA` | 9 | `GetDeviceState` |
| `IDirectInputDeviceA` | 10 | `GetDeviceData` |

Devices are identified by the GUID passed to `CreateDevice`
(`GUID_SysKeyboard` / `GUID_SysMouse`). Each hook calls the original first and
then adds injected state, so real input keeps working and injection composes
with it. The game asks for **DirectInput version 0x0500**.

Two details that matter. Vtables are shared by every instance of a class, so a
slot must only save the original the first time — a second pass would record our
own hook as the original and recurse forever. And the game reads input
**buffered**, through `GetDeviceData`, not `GetDeviceState`; overlaying the
polled state buffer alone drives nothing at all. Injected events are appended
to the array the real device returns, using whatever capacity is left over.
`GetDeviceData`'s `pdwInOut` is in/out, so the caller's capacity has to be
captured *before* calling the original, which overwrites it with the count.

### The control socket

`AM2_CONTROL=1` starts a listener on `127.0.0.1:31337` (`AM2_CTL_PORT`) speaking
a line protocol, each command answered with `ok ...` or `err ...` so a client can
wait for acknowledgement instead of guessing at timing:

```
key return tap          key down down 500       key a up
mouse move 40 -20       mouse left tap          state / clear / ping
counts                  dump <hex addr> [len]
```

`dump` hexdumps live memory, bounded to 96 bytes and guarded by
`IsBadReadPtr`. Sprite and glyph data only exists at runtime, so decoding an
encoding by hand needs a way to look at the real bytes — this is what settled
the sprite header and row-table layout.

Two related conveniences: the argument tracer now prints a pointer's value *and*
its text when the target looks like a string, rather than replacing one with the
other (showing only the text hid the address, which is exactly what you need in
order to dump it); and `make run WINEDBG=+seh` overrides the wine debug channel,
which is otherwise pinned to `-all`.

`tap` is a timed hold rather than an instant press-release pair, because the
game polls once per frame and an immediate release would fall between two polls
and never be seen.

```sh
tools/am2ctl.py key return tap      # one command
tools/am2ctl.py -f script.txt       # a script; `sleep N` pauses locally
tools/am2ctl.py                     # interactive
```

## Un-stubbing the 1999 logger

The retail build reduced the logger at `0x0045CAA0` to a bare `ret`, but did so
at the callee. All 623 call sites still push their real format strings.
Redirecting that address at a working implementation turns the original debug
commentary back on live, under `AM2_GAMELOG=1`:

```
system speed: 1
Using High Performance Counter
Lobby start: about to call ReadMpMapsFile
```

That last line names a function (`ReadMpMapsFile`) that static extraction never
found, because it is reached through a code path the string scan did not
attribute.

### Why it cannot just call vsnprintf

Un-stubbing naively **crashes the game** — reproducibly, on selecting Boot Camp.
A clean A/B settles it: `GAMELOG=1` dies, `GAMELOG=0` reaches the mission
briefing. The fault is the harness's, not the game's.

The cause is that `0x0045CAA0` is not only the logger. MSVC 6 folds identical
COMDATs (`/OPT:ICF`), so every function stubbed down to a bare `ret` merges to
one address. Its 623 callers are therefore callers of *several different*
functions with different signatures. Some pass a code address
(`0x004254DC`, `0x00403507`) or a small integer where the logger expects a
format string, and handing binary data to `_vsnprintf` means any byte pair
resembling `%s` dereferences garbage.

Nothing ever caught this because the body was a no-op for the game's entire
shipping life. So `game_log` formats defensively instead of trusting callers:

- the format string must itself be a readable, printable C string, or the call
  is reported as suspect and skipped;
- every `%s` argument is range-checked before being dereferenced, rendering as
  `<bad:ADDR>` when it fails;
- width, precision and flags are still handed back to the CRT one conversion at
  a time, so formatting stays correct — only argument fetching and validation
  are taken over.

With that, the logger runs through gameplay without crashing, and the suspect
call sites become evidence rather than a landmine.

## Running

`make run` is the only launch target; variations are make variables rather than
separate targets.

```sh
make run                # harness + the 1999 debug logger
make run TRACE=1        # argument-trace every patched function
make run OBSERVE=1      # log the observed functions' call sites
make run ARGS=          # drop the -nointro -dbg developer switches
make run-stock          # unpatched, for A/B comparison
```

It runs inside a Wine virtual desktop so the game cannot mode-switch the real
one, and honours `$DISPLAY`, so a headless run is just:

```sh
Xvfb :99 -screen 0 1024x768x24 &
DISPLAY=:99 make run OBSERVE=1
```

`tools/drive.sh` builds on that to click through menus and screenshot, for
exercising code paths that only run during real gameplay.

Output goes to `am2.log` in the game folder (override with `AM2_LOG`), and to
`OutputDebugStringA`. Harness lines and game lines share one sink so they
interleave in true order.

## Current targets

| address | function | status | evidence |
|---|---|---|---|
| `0x0045CAA0` | `Log` | replaced | un-stubbed, opt-in; 2,605 calls/session |
| `0x0042E1C0` | `RectSet` | **verified** | 161,955 calls; UI renders correctly |
| `0x0042DDE0` | `ApproxDist` | **verified** | 1,721 calls in gameplay |
| `0x0042E180` | `Clamp` | **verified** | 547,390 calls |
| `0x0042E1F0` | `PointInRect` | **verified** | 15,473,860 calls; rendering pixel-correct |
| `0x0042E220` | `ClipRect` | **verified** | 5,146,540 calls; sprites and terrain correct |
| `0x00446930` | `DrawText` | **verified** | every string on screen renders correctly |
| `0x0041C710` | `BlitGlyph` | **verified** | 874,768 calls; every text pixel written by our decoder |
| `0x00445FF0` | `DrawSprite` | **verified** | 61,531 calls; scene renders correctly |
| `0x0041C2B0` | `BlitCopy16` | **verified** | 72 calls |
| `0x0041C1C0` | `BlitCopy32` | **verified** | 508 calls |
| `0x0041C3A0` | `BlitRemap16` | **verified** | 441 calls |
| `0x0041B9A0` | `LockSurface` | **verified** | 919,302 calls |
| `0x0041BA40` | `UnlockSurface` | **verified** | 744,597 calls |
| `0x00446070` | `DrawSpriteClipped` | **verified** | 1,158,670 calls |
| `0x0041C480` | `BlitOverlay` | **verified** | reached; see the note below |
| `0x004464C0` | `EncodeGlyph` | **verified** | 672 glyphs encoded; round-trips through BlitGlyph |
| `0x004465E0` | `RenderGlyph` | **verified** | 672 calls; whole font pipeline is ours |

### Use the Windows headers

`src/inject/win32.h` is the single place that pulls in `windows.h` and
`ddraw.h`. Earlier headers dodged them with forward declarations of
`struct IDirectDrawSurface` and `struct HFONT__`, to avoid `winuser.h`'s
`DrawText` macro colliding with the game's reconstructed `DrawText`. That was
the wrong trade: this is Windows code calling Windows APIs, and the collision is
one problem to fix once rather than a reason to keep restating types. `win32.h`
includes both headers with `CINTERFACE`/`COBJMACROS` and `#undef DrawText`s
afterwards. Any future reconstruction that reuses a Win32 name should be undone
there too.

Note that `BlitCopy16/32` and `BlitRemap16` now report **0** calls. That is not a
regression: their only call sites were the dispatcher, which is now ours and
calls them directly as C functions rather than through the patched entry points.
The same thing happened to `FindSlot` and to `BlitGlyph`'s share of `DrawText`.
A reconstructed function's trace count only ever measures the *game's* remaining
call sites, and reaches zero once its last caller is reconstructed.

That creates a real blind spot, and `BlitOverlay` hit it: a count of 0 means
either "bypassed by a reconstructed caller" or "never called at all", and the
counter cannot tell them apart. Shadows on screen were not proof either, since
they could have been baked into the base sprites. Settling it took a temporary
probe inside the fill, which reported the path reached with a live table at
`0x00502BDC` and pixel counts of 1, 1 and 2 -- thin shadow edges, exactly as
expected. When a reconstructed function's callers are also reconstructed, reach
for a probe rather than reading the counter.

### Use the real SDK headers, do not restate them

`surface.c` gets `DDSURFACEDESC` and the `IDirectDrawSurface` vtable from
`ddraw.h`, not from hand-written copies. The first version of it declared both
by hand; every value happened to be right, but re-typing a public API's layout
is duplication that can only rot. The SDK confirms all of it anyway --
`sizeof(DDSURFACEDESC)` is 108 = the `0x6C` the game hardcodes, `lPitch` at
`0x10`, `lpSurface` at `0x24`, `DDLOCK_WAIT` is 1, and `DDERR_SURFACELOST` is
`0x887601C2`.

One trap that follows: `ddraw.h` pulls in `windows.h`, and `winuser.h` defines
`DrawText` as a macro expanding to `DrawTextA`. That collides with our
reconstructed `DrawText` the moment both headers meet. `surface.h` therefore
forward-declares `struct IDirectDrawSurface` and keeps the DirectDraw includes
inside `surface.c`. Expect the same clash for any reconstruction that reuses a
Win32 name.

### Check `ret N` before assuming a shared signature

The copy variants crashed on their first call, and the cause is worth
remembering. A normalised diff of `0x0041C1C0` against `0x0041C2B0` showed a
single meaningful difference, and one of `0x0041C3A0` against `0x0041C710`
showed only the inner fill — so all four looked like one function with a policy
knob. They are not. They differ in **arity**:

| | cleanup | stack args | why |
|---|---|---|---|
| `BlitGlyph` | `ret 0x18` | 6 | needs the colour |
| `BlitRemap16` | `ret 0x18` | 6 | needs the lookup table |
| `BlitCopy16` | **`ret 0x14`** | **5** | needs neither |
| `BlitCopy32` | **`ret 0x14`** | **5** | needs neither |

Declaring the copy variants with a sixth `unused` parameter made the compiler
emit `ret $0x18`, over-popping four bytes. Control returned to the game with
`esp` four bytes high, its next instruction read a local from the wrong slot,
got NULL, and faulted dereferencing it. The fault address was in the *game's*
code, several instructions after the call, which is exactly why reading the
reconstruction over and over found nothing.

Two lessons. **A diff that normalises away jump targets hides the epilogue**, so
compare `ret N` explicitly before assuming two functions share a signature. And
**the tracer's `nargs` is a guess that can invent arguments**: observing these
with `nargs=6` produced a sixth value that varied per call, which was caller
stack beyond the real five and not an argument at all. That varying value was
visible and unexplained for two rounds before it meant anything.

### Getting the fault address

`ShowCrashDialog=0` under `HKCU\Software\Wine\WineDbg` makes Wine print the
unhandled exception to stderr instead of opening the debugger window, which is
what a headless run needs:

```sh
WINEPREFIX=$PWD/.wine wine reg add 'HKCU\Software\Wine\WineDbg' \
    /v ShowCrashDialog /t REG_DWORD /d 0 /f
make run WINEDBG=+seh 2>&1 | grep -i "page fault"
```

The harness DLL is built with `-g`, so an address inside it maps back to a
source line; `i686-w64-mingw32-nm` gives the decorated names, whose `@N` suffix
is itself the parameter-size check that would have caught this bug.
| `0x00433810` | `PackKey` | **verified** | 3,986 calls |
| `0x00433830` | `KeyFieldA` | **verified** | 802 calls |
| `0x00433840` | `KeyFieldB` | **verified** | 80 calls |
| `0x00433850` | `KeyFieldC` | installed | **not exercised** — its one call site was never reached |
| `0x004235D0` | `CheckSaveTag` | **verified** | all 15 call sites hit, 317 items loaded, 0 errors |
| `0x004277A0` | `FindSlot` | **verified** | binary search over the object registry |
| `0x00427820` | `LookupByUID` | **verified** | 557,800 calls during Boot Camp movement |
| `0x00429740` | `AddToItemList` | **verified** | 1,609 registrations, all resolved by our own lookup |
| `0x00427850` | `FirstItem` | **verified** | 178,392 walks |
| `0x00427880` | `NextItem` | **verified** | 287,032,728 calls; count is exact (see below) |
| `0x00428590` | `RemoveFromItemList` | installed | **not yet exercised** — nothing is destroyed by moving around |
| `0x00433860` | `ObjIsItem` | **verified** | 750,457 calls |
| `0x00457470` | `ObjIsType2` | **verified** | 501,957 calls |
| `0x00457490` | `ObjIsType3` | **verified** | 1,840 calls |
| `0x00457420` | `ObjIsTypeIn238` | **verified** | 62,160,244 calls |

`NextItem` has an unusually crisp proof. Across a session it was called
287,032,728 times against 178,392 walks of a 1,609-entry table, and
178,392 x 1,609 is exactly 287,032,728. Every walk visited every object exactly
once — nothing skipped, nothing repeated — which is precisely what the stamp
logic has to guarantee and the hardest part of that reconstruction to get right.

### How CheckSaveTag was finally verified

It is on the savegame **load** path — it `fread`s a tag and compares — so it
needed a real load, not a save. Boot Camp cannot save (its in-game menu offers
only RETURN TO GAME / CONTROLS / AUDIO / ABORT MISSION), but SINGLE PLAYER →
SELECT → LOAD reaches a shipped `map0_mission0.sav`.

Loading it produced `Loaded 317 items`, zero `Error reading save file` messages,
and a correctly restored mission — and `counts` reported exactly **15** calls,
matching the 15 static call sites. Every `(file, line, tag)` triple recovered
statically in `savetags.tsv` was confirmed byte-for-byte at runtime.

Runtime also found what disassembly could not. The 15th site at `0x004268FF`
loads its arguments in registers, so `find_savetags.py` reports it as `?`; under
tracing it resolves to **`gameproc.cpp:2253`, tag `0x00000438`**, called twice
per load. Static and dynamic analysis each covered the other's blind spot.
