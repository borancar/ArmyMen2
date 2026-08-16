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

It is opt-in for a reason. The body was a no-op for the whole of the game's
shipping life, so any call site whose arguments drifted out of sync with its
format string was never noticed — and re-enabling the logger makes those
reachable. The very first call at startup passes a **NULL format string**;
`game_log` guards against it. Expect more of these.

## Running

```sh
make run        # patches installed, logger left stubbed
make run-log    # AM2_GAMELOG=1, the game's own debug output
```

Both run inside a Wine virtual desktop so the game cannot mode-switch the real
desktop. For a fully headless run:

```sh
Xvfb :99 -screen 0 1024x768x24 &
DISPLAY=:99 AM2_GAMELOG=1 AM2_TRACE=1 make run
```

Output goes to `am2.log` in the game folder (override with `AM2_LOG`), and to
`OutputDebugStringA`. Harness lines and game lines share one sink so they
interleave in true order.

## Current targets

| address | function | status |
|---|---|---|
| `0x0045CAA0` | `Log` | replaced — un-stubbed, opt-in |
| `0x004235D0` | `CheckSaveTag` | replaced — **not yet exercised** |

`CheckSaveTag` only runs when a savegame is loaded, and the install ships no
saves. It is patched and the game runs stably with it in place, but that is not
the same as having been proven correct. To exercise it: play far enough to save,
then load with `AM2_TRACE=1` and check each reported tag against
`docs/savetags.tsv`.
