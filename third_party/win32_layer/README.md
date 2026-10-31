# win32_layer

The Win32 and DirectX surface a late-1990s Windows game calls, implemented
over [SDL3](https://www.libsdl.org) for Linux: no Wine, and no Windows code at
run time. It was written for, and extracted from,
[ArmyMen2](https://github.com/borancar/ArmyMen2) — a source reconstruction of
*Army Men II* (3DO, 1999) — where it carries both the reconstructed game and,
under a small PE loader, the original `ArmyMen2.exe` itself.

It is not Wine and does not try to be. It provides what that game uses, as
faithfully as an A/B against the original needs: frame-exact rendering,
deterministic timing on demand, and the same heap answers on every host.

## What is here

| | |
|---|---|
| `src/kernel32.cpp`, `src/kernel32crt.cpp` | files, handles, threads, events, critical sections, heaps, the command line and environment, and the import table a PE loader asks by module and name (`w32l_export_lookup`) |
| `src/user32.cpp` | windows, the message queue, focus, cursor, `MessageBoxA` (to stderr) |
| `src/gdi32.cpp` | device contexts, palettes (with the 20 Windows static colours under `SYSPAL_STATIC`), fonts drawn with stb_truetype |
| `src/ddraw.cpp` | DirectDraw: 8-bit surfaces, palettes, Blt/BltFast with colour keys, Flip paced to 60 Hz |
| `src/dsound.cpp` | DirectSound buffers mixed by one host mixer; winmm's `mmio` and multimedia timers |
| `src/dinput.cpp` | DirectInput keyboard and buffered mouse |
| `src/dplay.cpp` | DirectPlay, enough to answer "no session" |
| `src/sdl.cpp` | the SDL host: window, presentation, input, audio device, clocks, lockstep and replay, and `main()` |
| `src/fixedheap.cpp` | a fixed-address heap, so a run's allocations land at the same addresses every time |
| `src/crt.cpp` | host C-runtime shims with Windows paths translated |
| `include/` | `windows.h`, `ddraw.h`, `dsound.h`, `dinput.h`, `dplay.h`, `mmsystem.h`, ... — declaring exactly what is implemented, so a new dependency is a compile error rather than a silent missing function |

Exclusive full-screen modes are accepted and not enforced: a program that
asks for 640x480 in 8 bits gets a window of that logical size and keeps
running its full-screen code paths. `smackw32.dll` is listed among the
emulated modules so that a program importing it gets zero-answering traps
(no films) rather than a failed load.

## Using it

```sh
make                                   # build/libwin32_layer.a
```

32-bit only (i386): the programs it serves assume 4-byte pointers. It needs an
i686-capable `g++` and SDL3's i686 development files (on Fedora,
`glibc-devel.i686`, `libstdc++-devel.i686`, `SDL3-devel.i686`).

Compile the application against this layer's headers and link the library:

```sh
g++ -m32 -isystem include -include callconv.h -c app.cpp
g++ -m32 app.o build/libwin32_layer.a -lSDL3 -lpthread -lm
```

The application provides:

- **`WinMain`**, declared in `include/windows.h`. The layer's `main()` sets up
  SDL and calls it.
- **`w32l_app_info`** (`src/platform.h`): its name (window title and SDL app
  name), version, reverse-DNS identifier, and the Windows path
  `GetModuleFileNameA` answers. The layer's own definition is weak and names a
  generic program.

Optional hooks are function pointers in `src/platform.h` the application may
set: `w32l_host_frame_hook`, `w32l_host_hotkey`, `w32l_host_cursor_set`/`_query`,
`w32l_host_lua`, `w32l_thread_attach`. Defining `W32L_DEVTOOLS` when compiling
the layer builds a development variant: F5 and F9 go to `w32l_host_hotkey`
instead of the program, and `W32L_AUDIO_DUMP` works.

## Switches

All read from the environment.

| variable | effect |
|---|---|
| `W32L_SCALE=N` | integer window scale (default: the largest that fits) |
| `W32L_RESIZABLE=1` | a free-size, letterboxed window instead of a fixed-size one |
| `W32L_VSYNC=1` | also wait for the display's vertical blank on present |
| `W32L_FPS=N` | the rate Flip paces to (default 60; 0 turns pacing off) |
| `W32L_NOSOUND=1` | open no audio device; `DirectSoundCreate` answers `DSERR_NODRIVER` |
| `W32L_AUDIO_DUMP=FILE` | write the mixed audio as raw stereo float (`W32L_DEVTOOLS` builds) |
| `W32L_FONT_NARROW`, `W32L_FONT_BLACK`, `W32L_FONT_DEFAULT` | TTF files to use instead of the built-in choices; `W32L_FONT_NORECORD` |
| `W32L_DRIVE_X=PATH` | where Windows drive `X:` is on the host |
| `W32L_FIXED_HEAP=0` | turn the fixed-address heap off; `W32L_DEV_NOREUSE`, `W32L_DEV_POISON`, `W32L_DEV_CANARY` check it |
| `W32L_LOCKSTEP=1` | deterministic run: clocks advance a fixed step per pump (`W32L_LOCKSTEP_MS`, default 1000/60); `W32L_LOCKSTEP_PACE=1` keeps real-time pacing |
| `W32L_REPLAY=FILE` | scripted input, applied by pump number |
| `W32L_STEP=SOCKET` | wait for a coordinator before every pump (two programs stepped side by side) |
| `W32L_FRAMELOG=FILE` | a hash of every presented frame; `W32L_FRAMEDUMP_AT=N` with `W32L_FRAMEDUMP_TO=FILE` writes frame N as PPM |
| `W32L_PLATFORM_QUIET=1`, `W32L_PLATFORM_DEBUG=1` | less or more of the layer's own stderr diagnostics |
| `W32L_BLT_TRACE=1`, `W32L_TRACE_DI=1` | trace blits or DirectInput traffic, within pumps `W32L_TRACE_FROM`..`W32L_TRACE_TO` |

## License

GNU General Public License, version 2 — see [`LICENSE`](LICENSE).
`src/stb/stb_truetype.h` is Sean Barrett's, under its own public-domain/MIT
terms (see the end of that file).
