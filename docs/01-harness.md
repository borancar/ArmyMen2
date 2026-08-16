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
```

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
| `0x004235D0` | `CheckSaveTag` | **verified** | all 15 call sites hit, 317 items loaded, 0 errors |
| `0x004277A0` | `FindSlot` | **verified** | binary search over the object registry |
| `0x00427820` | `LookupByUID` | **verified** | 557,800 calls during Boot Camp movement |
| `0x00429740` | `AddToItemList` | **verified** | 1,609 registrations, all resolved by our own lookup |

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
