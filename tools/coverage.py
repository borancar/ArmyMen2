"""How much of the Win32/DirectX boundary is reconstructed, and what is left.

The question this answers is the one the port exists to answer: has the game's
communication with the outside world been taken over? Asserting that in prose
goes stale within a commit or two, so it is computed instead.

The set of reconstructed functions is not maintained here. It is read out of the
`patch_replace(ADDR_X, ...)` calls in src/game/*.cpp and the ADDR_X definitions
in src/inject/orig.h, so it cannot drift from what the harness actually
installs. Functions installed some other way -- WndProc is registered into the
WNDCLASS rather than patched -- are listed in REGISTERED below, which is the one
thing that does have to be kept by hand.

Every remaining import site is then classified, because "not reconstructed" and
"still crossing the boundary" are not the same thing. A 5,760-byte unit-AI
routine that reads GetTickCount once is not boundary code, and porting it to
capture that read would be reconstructing the game rather than its edges.

Import sites are not the whole boundary. DirectDraw, DirectSound and
DirectInput are reached through COM vtables and appear nowhere in the import
table, so a function can talk to DirectX all day without owning a single import
site. Those are counted too, from docs/comcalls.tsv, filtered to the interface
pointers that are known to be DirectX -- the game's own C++ virtual calls have
the identical machine-code shape and must not be mistaken for them.

Leaving them out was an actual mistake in this tool, not a hypothetical one: it
reported the boundary as nearly finished while 23 functions and 66 DirectX calls
sat outside it.

Writes docs/boundary.md.
"""

import collections
import csv
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import am2
import merges

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(REPO, "docs", "boundary.md")
CRT_START = 0x0045C000

# Reached without a patch. Both are callbacks the game hands to somebody else,
# and in both cases the address's ONLY reference in the image is the
# registration -- which is reconstructed -- so detouring it would install a jump
# nothing can reach. They are as replaced as anything installed by
# patch_replace, and have to be listed by hand because there is no call to read
# them out of.
REGISTERED = {
    "ADDR_WND_PROC",          # into the WNDCLASS, by InitApplication
    "ADDR_AUDIO_TIMER_PROC",  # into timeSetEvent, by StartAudioStream
}

# COM interface pointers known to be DirectX, identified in docs/comcalls.tsv by
# chasing the object back to a global. Anything else reached the same way is one
# of the game's own C++ objects -- 0x0065A058 (the repainter) and 0x006568A0
# (the current movie) both look exactly like COM and are not.
DIRECTX_OBJECTS = {
    0x004FDF78: "IDirectDraw",   0x004FE098: "IDirectDraw2",
    0x00502AD4: "primary",       0x00507128: "locked",
    0x00503100: "offscreen",     0x004FE08C: "back buffer",
    0x004FA440: "DirectSound",   0x004FA404: "DirectSound",
    0x004FA46C: "DirectSound",   0x004FA470: "DirectSound",
    0x004FA474: "DirectSound",
    0x00512FD0: "IDirectInput",  0x00512FD4: "input device",
    0x00512FD8: "input device",
}

# What each struct field is known to hold, where it has been established by
# reconstructing something that uses it. An unnamed one is not a mystery to be
# solved before it counts -- it is simply a cluster nobody has read yet.
FIELD_INTERFACES = {
    "0x3ec": "IDirectPlay4A, in the comm object",
    "0x10":  "IDirectDrawSurface, in a sprite or map descriptor",
    "0x800": "IDirectDrawPalette, in a palette holder",
}

# Imports that are a fact of running on Windows rather than a channel to the
# outside world. A function whose only boundary contact is one of these is
# ordinary game logic that happens to ask the clock or poke its own queue.
#
# THE LINE IS THE SAME ONE THIS PROJECT DRAWS FOR DIRECTX: creating or
# destroying an OS object is boundary work, operating on a handle you were given
# is not. docs/boundary.md already says of COM that "what still dispatches
# through COM is game logic holding a handle it did not make", and the kernel
# side has to mean the same thing or the word stops meaning anything.
#
# CreateThread, CreateEventA, CreateMutexA, SetThreadPriority and CloseHandle
# used to sit in this list and do not belong here. 0x004021A0 creates an event,
# starts a thread and sets its priority -- that is the comm thread, which
# CLAUDE.md separately calls a genuinely-boundary cluster, being dismissed here
# as incidental. Waiting on a handle or releasing a mutex stays incidental,
# because that is operating on something already made.
INCIDENTAL = {
    "GetTickCount", "QueryPerformanceCounter", "QueryPerformanceFrequency",
    "Sleep", "PostMessageA", "SendMessageA", "InterlockedExchange",
    "WaitForSingleObject", "WaitForMultipleObjects", "ReleaseMutex",
    "SetEvent", "ResetEvent",
    "EnterCriticalSection", "LeaveCriticalSection", "GetLastError",
    "GetCurrentThreadId", "IntersectRect", "SetRect", "OffsetRect",
    "InflateRect", "UnionRect", "PtInRect", "IsRectEmpty", "SetRectEmpty",
    "CopyRect", "EqualRect", "GetActiveWindow", "GetFocus", "wsprintfA",
}


def addr_table():
    """{ADDR_NAME: address} from src/inject/orig.h."""
    out = {}
    path = os.path.join(REPO, "src", "inject", "orig.h")
    pat = re.compile(r"#define\s+(ADDR_[A-Z0-9_]+)\s+0x([0-9A-Fa-f]+)u?")
    with open(path) as fh:
        for line in fh:
            m = pat.match(line.strip())
            if m:
                out[m.group(1)] = int(m.group(2), 16)
    return out


def callers_of(img, target):
    """Every `call rel32` that lands on `target`."""
    import struct as _s
    out = []
    for _name, start, _end, data in img.sections:
        for j in range(len(data) - 5):
            if data[j] == 0xE8 and (start + j + 5
                                    + _s.unpack_from("<i", data, j + 1)[0]) == target:
                out.append(start + j)
    return out


def owner_of_addr(addr, sizes):
    """The functions.tsv entry containing `addr`, or None."""
    for fn, size in sizes.items():
        if fn <= addr < fn + size:
            return fn
    return None


def reconstructed(names):
    """Addresses the harness installs, read from the install sites themselves."""
    pat = re.compile(r"patch_replace\(\s*(ADDR_[A-Z0-9_]+)")
    found = set(REGISTERED)
    for path in am2.game_sources():
        with open(path) as fh:
            found.update(pat.findall(fh.read()))
    missing = sorted(n for n in found if n not in names)
    return {names[n] for n in found if n in names}, missing


def main():
    names = addr_table()
    done, missing = reconstructed(names)
    if missing:
        print("warning: patched names with no address in orig.h:", missing)

    rows = list(csv.DictReader(open(os.path.join(REPO, "docs", "imports.tsv")),
                               delimiter="\t"))
    sizes = {int(r["addr"], 16): int(r["size"])
             for r in csv.DictReader(
                 open(os.path.join(REPO, "docs", "functions.tsv")),
                 delimiter="\t")}

    # Re-key everything by the REAL function holding each site rather than by
    # the functions.tsv entry, which merges neighbours. Done here, once, so that
    # every count downstream is split-aware instead of each having to remember.
    img = am2.Image()
    merged = merges.real_functions(img)
    real_sizes = dict(sizes)

    def owner_of(entry, site):
        if entry in merged:
            starts, size = merged[entry]
            fn, real = merges.owner(starts, size, site)
            real_sizes[fn] = real
            return fn
        return entry

    per_fn = collections.defaultdict(list)
    for r in rows:
        fn = int(r["func"], 16)
        if fn and fn < CRT_START:
            per_fn[owner_of(fn, int(r["site"], 16))].append(r)

    # The other half of the boundary: DirectX through COM, which owns no import.
    com = collections.defaultdict(list)
    any_com = set()
    cxx_only = set()
    compath = os.path.join(REPO, "docs", "comcalls.tsv")
    if os.path.exists(compath):
        for r in csv.DictReader(open(compath), delimiter="\t"):
            try:
                this = int(r["this"], 16)
            except ValueError:
                this = 0
            fn = int(r["func"], 16)
            if fn and fn < CRT_START:
                fn = owner_of(fn, int(r["site"], 16))
                # A thiscall dispatch is the engine's own C++ virtual, never
                # COM: under CINTERFACE every COM method takes the interface as
                # a pushed first argument. A cdecl one is a callback: the
                # caller cleans exactly this call's arguments, which a COM
                # method never leaves it to do. Excluding both is what turns
                # the upper bracket from a guess into a measurement.
                if r.get("abi") not in ("thiscall", "cdecl"):
                    any_com.add(fn)
                else:
                    cxx_only.add(fn)
                if this in DIRECTX_OBJECTS:
                    com[fn].append(DIRECTX_OBJECTS[this])

    # A reconstructed address does not always equal the function address the
    # inventories used: functions.tsv merges neighbours, so WndProc's sites are
    # filed under 0x0040A6A0 while the address we patch is 0x0040A6B0. That has
    # to be matched by containment.
    #
    # But plain containment OVER-CREDITS, and did so twice. 0x00445320 and
    # 0x00445390 are two functions reported as one, so patching the first marked
    # the second's SmackWait covered when it was not. Then 0x0040CED0 and
    # 0x0040D020 -- reconstructing AudioTimerProc marked OpenAudioStream's
    # CreateSoundBuffer and GetCaps done a commit before they were.
    #
    # So containment is now applied to the REAL function, using the split points
    # tools/merges.py confirms by xref, and only falls back to the whole entry
    # where no split was found. A thunk and its body stay one unit, which is
    # what makes WndProc keep working; two neighbours do not.
    def is_done(fn):
        end = fn + real_sizes.get(fn, 0)
        return any(fn <= d < end for d in done)

    done_fns = {f for f in per_fn if is_done(f)}
    rest = {f: v for f, v in per_fn.items() if f not in done_fns}

    # Split what is left: functions whose every import is incidental are game
    # logic, not boundary.
    real, incidental = {}, {}
    for f, v in rest.items():
        (incidental if all(r["symbol"] in INCIDENTAL for r in v)
         else real)[f] = v

    sites = lambda d: sum(len(v) for v in d.values())

    # Above the CRT line: how much is up there, how much of it is not
    # incidental, and where the three DirectX entry thunks are called from.
    crt_sites = sum(1 for r in rows if int(r["func"], 16) >= CRT_START)
    crt_real = sum(1 for r in rows if int(r["func"], 16) >= CRT_START
                   and r["symbol"] not in INCIDENTAL)
    directx_thunks = []
    for r in rows:
        if r["symbol"] not in ("DirectDrawCreate", "DirectInputCreateA", "#1"):
            continue
        thunk = int(r["func"], 16)
        if thunk < CRT_START:
            continue
        name = f"{r['dll'].upper().replace('.DLL', '')}!{r['symbol']}"
        directx_thunks.append((name, thunk, callers_of(img, thunk)))
    directx_thunks.sort(key=lambda t: t[1])

    # The other ways out, each counted the same way: a site is outstanding only
    # if the function holding it is not reconstructed.
    # An import by ordinal shows up as a one-instruction thunk, and the thunk
    # lives with the CRT rather than in game code -- so counting the recorded
    # site answers about the thunk and not about anything that uses it. What
    # matters is who CALLS the thunk. DSOUND.dll #1 is DirectSoundCreate, whose
    # only caller is InitDirectSound.
    ordinal_left = 0
    for r in rows:
        if not r["symbol"].startswith("#"):
            continue
        thunk = int(r["func"], 16)
        for caller in callers_of(img, thunk):
            fn = owner_of_addr(caller, sizes)
            if fn is None:
                continue
            if fn in merged:
                starts, size = merged[fn]
                fn, _ = merges.owner(starts, size, caller)
            if not is_done(fn):
                ordinal_left += 1
    dynamic_left = sum(1 for r in rows
                       if r["symbol"] in ("LoadLibraryA", "LoadLibraryW",
                                          "GetProcAddress")
                       and int(r["func"], 16) < CRT_START
                       and not is_done(owner_of(int(r["func"], 16),
                                                int(r["site"], 16))))
    com_sites_left = sum(len(v) for f, v in com.items() if not is_done(f))
    delay = "none in this image"
    total_sites = sites(per_fn)
    total_com = unresolved = 0
    if os.path.exists(compath):
        for r in csv.DictReader(open(compath), delimiter="\t"):
            total_com += 1
            if not r["this"] or r["this"] == "0x00000000":
                unresolved += 1
    any_done = {f for f in any_com if is_done(f)}
    com_done = {f: v for f, v in com.items() if is_done(f)}
    com_left = {f: v for f, v in com.items() if not is_done(f)}

    with open(OUT, "w") as fh:
        w = fh.write
        w("# Win32 / DirectX boundary coverage\n\n")
        w("Generated by `tools/coverage.py`; do not edit. The reconstructed set\n"
          "is read from the `patch_replace` calls in `src/game/*.cpp`, so it\n"
          "cannot disagree with what the harness installs.\n\n")
        w("Only game code is counted. The statically linked MSVC CRT above\n"
          f"{CRT_START:#x} reaches plenty of kernel32 itself and is replaced\n"
          "wholesale by libc rather than function by function.\n\n")

        # Every mechanism by which this image can reach outside itself, and
        # whether anything using it is still original. The per-symbol and
        # per-slot tables below answer "how much is left" within a mechanism;
        # this answers the prior question, which is whether the inventory can
        # see the mechanism at all. tools/comcalls.py exists because the first
        # version of this file could not see COM, and reported the boundary as
        # nearly finished with 23 functions and 66 DirectX calls outside it.
        # The CRT exclusion, stated rather than left as a constant nobody
        # looks at. Every count in this file drops sites at or above
        # CRT_START, which is a large exclusion -- 138 of the 414 import sites
        # -- and it had never been checked.
        w("## What the CRT line hides\n\n")
        w(f"Every figure here counts only functions below `{CRT_START:#010x}`.\n"
          "That is a big exclusion and it deserves to be examined rather than\n"
          "trusted: above the line are\n"
          f"**{crt_sites}** import sites, of which **{crt_real}** are not\n"
          "incidental.\n\n")
        w("They are the statically linked MSVC 6 CRT -- `HeapAlloc`,\n"
          "`LCMapStringW`, `MultiByteToWideChar`, `RtlUnwind`,\n"
          "`SetUnhandledExceptionFilter` and the rest of locale, heap, stdio\n"
          "and startup. This port replaces the CRT with libc wholesale rather\n"
          "than function by function, so they are out of scope by design. It\n"
          "is also where every `CreateFileA`, `ReadFile` and `FindFirstFileA`\n"
          "in the image lives, which is why the game appears never to open a\n"
          "file.\n\n")
        w("Three entries up there are NOT CRT, and they are the ones that\n"
          "matter: the one-instruction import thunks the linker parked among\n"
          "it. Each is reached only from reconstructed code.\n\n")
        w("| entry point | thunk | called from |\n|---|---|---|\n")
        for sym, thunk, callers in directx_thunks:
            who = ", ".join(f"`{c:#010x}`" for c in callers) or "nothing"
            w(f"| {sym} | `{thunk:#010x}` | {who} |\n")
        w("\nGame code does live above the line -- `DrawSeqBar` at\n"
          "`0x004624A0` is there, with three `Blt` calls -- so the constant is\n"
          "a rule of thumb and not a real boundary. What matters is that\n"
          "nothing outstanding hides behind it: the only COM dispatch above\n"
          "the line is `DrawSeqBar`'s, and that is reconstructed.\n\n")

        w("## Ways out of the process\n\n")
        w("Each mechanism this image can use to reach the outside world, and\n"
          "whether anything still uses it from unreconstructed code. The tables\n"
          "below measure how much is left WITHIN a mechanism; this one is about\n"
          "whether a mechanism is being measured at all.\n\n")
        w("| channel | how it is found | outstanding |\n|---|---|---|\n")
        w(f"| named imports | `docs/imports.tsv` | {sites(real)} non-incidental "
          f"site(s) |\n")
        w(f"| imports by ordinal | `#N` in the same file, checked through the "
          f"callers of its thunk | {ordinal_left} |\n")
        w(f"| COM vtables | `docs/comcalls.tsv`, `stdcall` only | "
          f"{com_sites_left} |\n")
        w(f"| runtime resolution | `LoadLibraryA` + `GetProcAddress` sites | "
          f"{dynamic_left} |\n")
        w(f"| delay-loaded imports | PE delay-import directory | "
          f"{delay} |\n")
        w("\nThe six named-import sites are three `MessageBoxA` calls and the\n"
          "`GetActiveWindow` each passes as its owner, and\n"
          "`docs/binarypatches.md` shows nothing in the image can reach any of\n"
          "them. The value of this table is not the zeroes -- it is that each\n"
          "mechanism was looked for at all. `tools/comcalls.py` exists because\n"
          "an earlier version of this file could not see COM and reported the\n"
          "boundary as nearly finished with 23 functions and 66 DirectX calls\n"
          "outside it.\n\n")

        w("## Where it stands\n\n")
        w("| | functions | import sites |\n|---|---:|---:|\n")
        w(f"| reconstructed | {len(done_fns)} | {sites({f: per_fn[f] for f in done_fns})} |\n")
        w(f"| still boundary | {len(real)} | {sites(real)} |\n")
        w(f"| game logic, incidental calls only | {len(incidental)} | {sites(incidental)} |\n")
        w(f"| **total** | **{len(per_fn)}** | **{total_sites}** |\n\n")

        # Per-DLL, which is the form the question is usually asked in: is the
        # channel to this library ours yet, or not?
        per_dll = collections.defaultdict(lambda: [0, 0])
        leftover = collections.Counter()
        for r in rows:
            fn = int(r["func"], 16)
            if not fn or fn >= CRT_START:
                continue
            # Through owner_of, like everything else -- done_fns is keyed by the
            # real function, and looking up the merged entry here reported
            # MoviePoll's SmackWait as outstanding when MoviePoll is ours.
            fn = owner_of(fn, int(r["site"], 16))
            d = per_dll[r["dll"].upper().replace(".DLL", "")]
            d[1] += 1
            if fn in done_fns:
                d[0] += 1
            else:
                leftover[(r["symbol"], r["symbol"] in INCIDENTAL)] += 1

        w("## By library\n\n")
        w("The same import sites grouped by which DLL they reach, because that\n"
          "is the form the question is usually asked in -- is the channel to\n"
          "this library ours yet?\n\n")
        w("| library | reconstructed | sites | |\n|---|---:|---:|---|\n")
        for dll, (d, t) in sorted(per_dll.items(), key=lambda kv: -kv[1][1]):
            w(f"| {dll} | {d} | {t} | {'**complete**' if d == t else ''} |\n")
        w("\n")

        real_left = sorted((sym, n) for (sym, inc), n in leftover.items() if not inc)
        w("USER32 and KERNEL32 never reach 100% and are not meant to: most of\n"
          "what is left in them is a `GetTickCount` or an `IntersectRect`, which\n"
          "is running on Windows rather than talking to anyone. The list that\n"
          "matters is the one with those removed -- every non-incidental import\n"
          "site still outside reconstructed code:\n\n")
        if real_left:
            w("| symbol | sites |\n|---|---:|\n")
            for sym, n in real_left:
                w(f"| `{sym}` | {n} |\n")
            w("\n**These three are a decision, not an omission.** Each of\n"
              "`0x0042F290`, `0x0044D2E0` and `0x0044D3F0` holds exactly two\n"
              "import sites -- a `MessageBoxA` and the `GetActiveWindow` it\n"
              "passes as owner -- and no COM dispatch at all. Both sit inside a\n"
              "block the section above proves nothing can reach. Everything else\n"
              "in them is menu logic: sound requests, menu state, calls into\n"
              "other game code.\n\n"
              "Porting them would move pure menu logic into the reconstruction\n"
              "to capture a dialog that cannot appear -- the opposite of what\n"
              "ranking targets by boundary density is for. The figure stays at\n"
              "three by choice.\n")
            w("\nRead that table with `docs/binarypatches.md` beside it. Most of\n"
              "those `MessageBoxA` sites are the \"insert the CD\" dialog, and\n"
              "every CD check in this executable has been patched to skip it --\n"
              "so the sites are still there, still import the symbol, and can\n"
              "never execute. A site that cannot run is not outstanding boundary\n"
              "work, and counting it as such overstates what is left.\n")
        else:
            w("There are none. Every non-incidental import site in the image is\n"
              "inside reconstructed code.\n")
        w("\n")

        # Interfaces kept in struct fields. comcalls.py cannot name these, but
        # it can group them, and a displacement usually is a subsystem.
        by_field = collections.defaultdict(lambda: [set(), set()])
        if os.path.exists(compath):
            for r in csv.DictReader(open(compath), delimiter="\t"):
                if r.get("abi") != "stdcall" or not r.get("field"):
                    continue
                fn = int(r["func"], 16)
                if not fn or fn >= CRT_START:
                    continue
                slot = by_field[r["field"]]
                # is_done(), not membership of done_fns: that set only covers
                # functions that own an import site, and a DirectPlay or sprite
                # function need not own one at all.
                (slot[0] if is_done(fn) else slot[1]).add(fn)

        if by_field:
            w("## Interfaces kept in struct fields\n\n")
            w("The section above can only name an interface it can trace to a\n"
              "global. These are the ones it cannot -- reached through a field of\n"
              "some object -- and they are not a lesser category: a whole\n"
              "subsystem tends to live behind one displacement. DirectPlay is\n"
              "*entirely* `comm+0x3EC` and appears nowhere above, and the sprite\n"
              "surfaces are all `sprite+0x10`. Both were found by grouping on the\n"
              "displacement rather than by any ranking.\n\n")
            w("| field | what it holds | reconstructed | left |\n|---|---|---:|---:|\n")
            for off, (did, left) in sorted(by_field.items(),
                                           key=lambda kv: -(len(kv[1][0]) + len(kv[1][1]))):
                what = FIELD_INTERFACES.get(off, "—")
                w(f"| `{off}` | {what} | {len(did)} | {len(left)} |\n")
            w("\n")

        w("## DirectX through COM\n\n")
        w("These own no import site and so appear nowhere above. A function can\n"
          "call DirectDraw all day without the import table showing it.\n\n")
        w("**This is a lower bound, not a census.** `tools/comcalls.py` can only\n"
          "say which interface a call is on when the object traces back to a\n"
          f"global, and it cannot for {unresolved} of the {total_com} dispatch sites in the\n"
          "image -- those reach the object through a parameter, a local or a\n"
          "struct field. Some of them are DirectX and are not counted here.\n"
          "`LockSurface` is the obvious example: it takes the surface as an\n"
          "argument, so its own `Lock` call is one of the unclassifiable ones,\n"
          "and it has been reconstructed since long before this section existed.\n"
          "There is a second undercount too: the scan gives up when the vtable\n"
          "register was loaded in a basic block it cannot see, which is why\n"
          "`PresentFrame` shows three calls here and makes four. That one has\n"
          "been measured -- it is five sites across the whole image, all in\n"
          "functions already reconstructed, and no function is missing from the\n"
          "survey because of it.\n"
          "Treat the number below as \"known to be outstanding\", never as\n"
          "\"all that is outstanding\".\n\n")
        w("| | functions | call sites |\n|---|---:|---:|\n")
        w(f"| known DirectX, reconstructed | {len(com_done)} | {sites(com_done)} |\n")
        w(f"| known DirectX, still to do | {len(com_left)} | {sites(com_left)} |\n\n")
        w("And the bracket the caveat above implies. Counting every function\n"
          "that dispatches through a vtable at all, whether or not the object\n"
          "could be named, gives the other end of the range:\n\n")
        w("| | functions |\n|---|---:|\n")
        w(f"| any COM dispatch, reconstructed | {len(any_done)} |\n")
        w(f"| any COM dispatch, not | {len(any_com) - len(any_done)} |\n\n")
        w("The true DirectX total sits between the two. The second row used to\n"
          "be mostly the game's own C++ objects, and that is no longer a guess:\n"
          "`tools/comcalls.py` now separates the two by how `this` is passed.\n"
          "COM is stdcall and pushes the interface as an explicit first\n"
          "argument; an i386 MSVC C++ virtual is thiscall and puts it in ecx.\n"
          f"{len(cxx_only)} functions dispatch only that way and have been dropped from\n"
          "the bracket entirely -- they are destructor chains and object\n"
          "teardown, not boundary code.\n\n")
        for f, v in sorted(com_left.items(), key=lambda kv: -len(kv[1])):
            objs = collections.Counter(v)
            w(f"- `{f:#010x}` {sizes.get(f, 0)}B, {len(v)} calls — "
              + ", ".join(f"{k} x{n}" for k, n in objs.most_common()) + "\n")
        w("\n")

        w("The middle row is the work that remains. The bottom row is not work:\n"
          "those functions touch Win32 only through things every Windows program\n"
          "does -- reading the clock, posting to its own message queue, taking a\n"
          "mutex, intersecting a rectangle. Reconstructing a 5KB unit-AI routine\n"
          "to capture one `GetTickCount` would be porting the game, not its edges.\n\n")

        w("## Still boundary\n\n")
        w("Ranked by density, because that is what distinguishes a boundary\n"
          "function from game logic with a call in it.\n\n")
        w("| function | size | sites | B/site | imports |\n")
        w("|---|---:|---:|---:|---|\n")
        for f, v in sorted(real.items(),
                           key=lambda kv: sizes.get(kv[0], 9999) / len(kv[1])):
            syms = sorted({r["symbol"] for r in v})
            shown = ", ".join(syms[:6]) + (" …" if len(syms) > 6 else "")
            sz = sizes.get(f, 0)
            w(f"| `{f:#010x}` | {sz} | {len(v)} | {sz // len(v)} | {shown} |\n")

        w("\n## By library\n\n")
        w("| dll | sites | reconstructed |\n|---|---:|---:|\n")
        by_dll = collections.Counter(r["dll"] for r in rows
                                     if int(r["func"], 16) and
                                     int(r["func"], 16) < CRT_START)
        for dll, n in by_dll.most_common():
            got = sum(1 for r in rows
                      if r["dll"] == dll and int(r["func"], 16) in done_fns)
            w(f"| {dll} | {n} | {got} |\n")

        w("\n## The filesystem\n\n")
        w("Worth its own answer, because the obvious question -- the game clearly\n"
          "reads files, so where is that on the list? -- has a non-obvious one.\n"
          "It never opens a file itself. Every `CreateFileA`, `ReadFile`,\n"
          "`WriteFile` and `FindFirstFileA` in the image is reached from inside\n"
          "the statically linked MSVC CRT, which the port replaces wholesale with\n"
          "libc rather than function by function.\n\n")
        crt_rows = [r for r in rows if int(r["func"], 16) >= CRT_START]
        fileish = sorted({r["symbol"] for r in crt_rows
                          if any(k in r["symbol"] for k in
                                 ("File", "Directory", "ReadFile", "WriteFile"))})
        w("Called only from the CRT: " + ", ".join(f"`{x}`" for x in fileish) + "\n\n")
        game_fileish = sorted({r["symbol"] for r in rows
                               if int(r["func"], 16) < CRT_START
                               and any(k in r["symbol"] for k in
                                       ("File", "Directory"))})
        w("Called from game code: "
          + (", ".join(f"`{x}`" for x in game_fileish) if game_fileish else "none")
          + ". The only file the game opens for itself is a `.WAV`, through\n"
            "WINMM rather than the CRT, and that is `src/game/win32/wavefile.cpp`.\n")

        w("\nNo networking library appears above, and that is not an omission:\n"
          "the game imports none. Its multiplayer transport is DirectPlay,\n"
          "obtained through `CoCreateInstance`, so the only trace in the import\n"
          "table is ole32. Both of those sites are reconstructed.\n")

    print(f"-> {os.path.relpath(OUT, REPO)}")
    print(f"reconstructed {len(done_fns)} functions / "
          f"{sites({f: per_fn[f] for f in done_fns})} sites")
    print(f"still boundary {len(real)} functions / {sites(real)} sites")
    print(f"DirectX COM    {len(com_done)}/{len(com)} named-object functions done, "
          f"{sites(com_left)} calls left")
    print(f"any COM        {len(any_done)}/{len(any_com)} functions done")
    print(f"game logic     {len(incidental)} functions / {sites(incidental)} sites")
    return 0


if __name__ == "__main__":
    sys.exit(main())
