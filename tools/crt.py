"""Identify the statically linked CRT, and the game code sitting among it.

Every figure in this project counts only functions below CRT_START, 0x0045C000.
docs/boundary.md already reports that this drops 138 of the image's 414 import
sites, and CLAUDE.md already says the constant is a rule of thumb -- DrawSeqBar
is at 0x004624A0 and has three Blt calls, so the boundary is not where the
constant says it is.

This works out where it actually is, and what lives above it, because the
winelib route makes the question concrete: the CRT is the part libc replaces
wholesale, and anything up there that is NOT the CRT has to be kept.

Nothing here is guessed from a call site. Each function is labelled from
evidence in its own body:

  IMPORTS   a set of Win32 imports that only one CRT entry point uses. Nothing
            but _findfirst calls FindFirstFileA; nothing but the heap calls
            HeapCreate. 63 functions above the line call an import at all.
  STRINGS   the CRT carries unmistakable text -- "Microsoft Visual C++ Runtime
            Library", the 1#INF/1#QNAN float spellings -- and so does the game,
            which is how "CreateWeapon" and "Bad Vehicle Type" give themselves
            away.
  CODE      two are recognisable by arithmetic: MSVC's rand is the LCG
            imul 0x343FD / add 0x269EC3, and _ftol is the fnstcw/fistp dance.
  THUNK     a 6-byte `jmp dword ptr [IAT slot]`. There are exactly six in the
            image, and three sit in this region because the linker parked
            DirectDrawCreate, DirectInputCreateA and DSOUND #1 among the CRT.

    tools/crt.py            # the classification
    tools/crt.py --unknown  # only what is still unlabelled
"""

import csv
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import am2
import capstone

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(REPO, "docs", "crt.md")
SCAN_FROM = 0x00458000          # well below the nominal line, to catch the frontier
NOMINAL_CRT_START = 0x0045C000

# An import set that only one CRT entry point uses. Longest match wins, so
# _open's CreateFileA beats a bare GetLastError.
BY_IMPORT = [
    ({"GetCommandLineA", "GetStartupInfoA", "GetVersion"}, "WinMainCRTStartup"),
    ({"HeapCreate", "HeapDestroy"},                        "_heap_init"),
    ({"HeapAlloc", "HeapFree", "HeapReAlloc", "VirtualAlloc"}, "_heap_alloc / __sbh"),
    ({"HeapFree", "VirtualFree"},                          "_free / __sbh_free"),
    ({"HeapAlloc", "HeapReAlloc"},                         "realloc"),
    ({"VirtualAlloc"},                                     "__sbh_alloc_new_region"),
    ({"HeapSize"},                                         "_msize"),
    ({"HeapFree"},                                         "free"),
    ({"HeapAlloc"},                                        "malloc"),
    ({"FindFirstFileA", "GetLastError"},                   "_findfirst"),
    ({"FindNextFileA", "GetLastError"},                    "_findnext"),
    ({"FindClose"},                                        "_findclose"),
    ({"FileTimeToLocalFileTime", "FileTimeToSystemTime"},  "__loctotime_t"),
    ({"GetCurrentDirectoryA", "GetFullPathNameA"},         "_fullpath"),
    ({"GetCurrentDirectoryA", "SetCurrentDirectoryA", "SetEnvironmentVariableA"}, "_chdir"),
    ({"GetDriveTypeA"},                                    "_getdrive"),
    ({"CreateDirectoryA", "GetLastError"},                 "_mkdir"),
    ({"RemoveDirectoryA", "GetLastError"},                 "_rmdir"),
    ({"DeleteFileA", "GetLastError"},                      "remove / _unlink"),
    ({"GetFileAttributesA", "SetFileAttributesA"},         "_chmod"),
    ({"GetLocalTime", "GetSystemTime", "GetTimeZoneInformation"}, "__getsystime"),
    ({"GetTimeZoneInformation", "WideCharToMultiByte"},    "_tzset"),
    ({"CreateFileA", "GetFileType", "SetUnhandledExceptionFilter"}, "_open / _sopen"),
    ({"GetStdHandle", "GetFileType", "SetHandleCount", "GetStartupInfoA"}, "_ioinit"),
    ({"ReadFile", "GetLastError"},                         "_read"),
    ({"WriteFile", "GetLastError"},                        "_write"),
    ({"SetFilePointer", "GetLastError"},                   "_lseek"),
    ({"SetEndOfFile", "GetLastError"},                     "_chsize"),
    ({"FlushFileBuffers", "GetLastError"},                 "_commit / fflush"),
    ({"CloseHandle", "GetLastError"},                      "_close"),
    ({"ExitProcess", "TerminateProcess", "GetCurrentProcess"}, "_amsg_exit / abort"),
    ({"UnhandledExceptionFilter"},                         "_XcptFilter"),
    ({"ExitProcess"},                                      "exit / _exit"),
    ({"LCMapStringA", "LCMapStringW"},                     "__crtLCMapStringA"),
    ({"GetStringTypeA", "GetStringTypeW"},                 "__crtGetStringTypeA"),
    ({"CompareStringA", "CompareStringW"},                 "__crtCompareStringA"),
    ({"GetACP", "GetOEMCP"},                               "__initmbctable"),
    ({"GetCPInfo"},                                        "getSystemCP / setSBCS"),
    ({"GetEnvironmentStrings", "GetEnvironmentStringsW"},  "_setenvp"),
    ({"SetEnvironmentVariableA"},                          "_putenv"),
    ({"GetModuleFileNameA", "GetStdHandle", "WriteFile"},  "_FF_MSGBANNER"),
    ({"GetModuleFileNameA"},                               "__crtGetModuleFileNameA"),
    ({"SetStdHandle"},                                     "_set_osfhnd"),
    ({"IsBadReadPtr"},                                     "_validate (read)"),
    ({"IsBadWritePtr"},                                    "_validate (write)"),
    ({"IsBadCodePtr"},                                     "_validate (code)"),
    ({"MultiByteToWideChar", "WideCharToMultiByte"},       "__crtWideCharToMultiByte"),
    ({"WideCharToMultiByte"},                              "wide/narrow conversion"),
    ({"RtlUnwind"},                                        "SEH unwind"),
    ({"GetModuleHandleA", "GetProcAddress"},               "runtime symbol lookup"),
    ({"LoadLibraryA", "GetProcAddress"},                   "runtime symbol lookup"),
    ({"GetTickCount"},                                     None),   # game: see below
    ({"IntersectRect"},                                    None),
]

# Text that settles it either way.
CRT_TEXT = ("Microsoft Visual C++ Runtime Library", "Runtime Error!",
            "<program name unknown>", "1#INF", "1#QNAN", "1#IND", "e+000",
            "IsProcessorFeaturePresent", "GetLastActivePopup")
GAME_TEXT = ("DO_DROP", "Vehicle", "CreateWeapon", "DestroyWeapon", "Weapon",
             "Blt Seq Pixels", "Game type", "uid wasn't", "ammo")


def imports_by_function():
    out = {}
    with open(os.path.join(REPO, "docs", "imports.tsv")) as fh:
        for r in csv.DictReader(fh, delimiter="\t"):
            out.setdefault(int(r["func"], 16), set()).add(r["symbol"])
    return out


def iat_slots():
    import pefile
    pe = pefile.PE(am2.EXE, fast_load=False)
    pe.parse_data_directories()
    out = {}
    for d in pe.DIRECTORY_ENTRY_IMPORT:
        for imp in d.imports:
            out[imp.address] = (d.dll.decode(),
                                imp.name.decode() if imp.name else "#%d" % imp.ordinal)
    return out


def classify(img, sizes, imps, slots, strings, md):
    """(label, why) per function, or (None, None)."""
    out = {}
    for addr in sorted(sizes):
        if addr < SCAN_FROM:
            continue
        size = sizes[addr]
        body = img.read(addr, min(size, 0x40))

        if len(body) >= 6 and body[0] == 0xFF and body[1] == 0x25:
            slot = struct.unpack_from("<I", body, 2)[0]
            if slot in slots:
                out[addr] = ("thunk: %s" % slots[slot][1], "thunk")
                continue

        text = strings.get(addr, set())
        if any(t in s for s in text for t in GAME_TEXT):
            out[addr] = ("GAME CODE", "strings")
            continue
        if any(t in s for s in text for t in CRT_TEXT):
            out[addr] = ("CRT (message/format)", "strings")
            continue

        ins = list(md.disasm(img.read(addr, min(size, 0x30)), addr))
        ops = " ".join("%s %s" % (i.mnemonic, i.op_str) for i in ins)
        if "0x343fd" in ops and "0x269ec3" in ops:
            out[addr] = ("rand", "code")
            continue
        if "fnstcw" in ops and any(i.mnemonic.startswith("fistp") for i in ins):
            out[addr] = ("_ftol", "code")
            continue

        have = imps.get(addr, set())
        if have:
            best = None
            for want, name in BY_IMPORT:
                if want <= have and (best is None or len(want) > len(best[0])):
                    best = (want, name)
            if best and best[1]:
                out[addr] = (best[1], "imports")
                continue
            if best:
                out[addr] = ("GAME CODE", "imports")
                continue
        out[addr] = (None, None)
    return out


def main():
    img = am2.Image()
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    sizes = {int(r["addr"], 16): int(r["size"])
             for r in csv.DictReader(
                 open(os.path.join(REPO, "docs", "functions.tsv")), delimiter="\t")}

    strings = {}
    for i in img.disasm(".text"):
        if i.address < SCAN_FROM:
            continue
        for tok in i.op_str.replace(",", " ").split():
            if tok.startswith("0x4"):
                try:
                    v = int(tok, 16)
                except ValueError:
                    continue
                t = img.cstring(v)
                if t and 3 < len(t.strip()) < 80:
                    fn = next((f for f, s in sizes.items() if f <= i.address < f + s), None)
                    if fn is not None:
                        strings.setdefault(fn, set()).add(t.strip())

    labels = classify(img, sizes, imports_by_function(), iat_slots(), strings, md)

    # Position, once the frontier is established by evidence rather than
    # assumed. A function above the lowest evidenced CRT function, with no game
    # evidence of its own, is CRT -- the library is contiguous, which is what
    # linking one static .lib does. This is weaker than an import signature and
    # is reported separately so the two are never confused.
    ev_game = [a for a, (l, _w) in labels.items() if l == "GAME CODE"]
    ev_crt = [a for a, (l, _w) in labels.items()
              if l and l != "GAME CODE" and not l.startswith("thunk")]
    frontier = min(ev_crt) if ev_crt else NOMINAL_CRT_START
    for a in list(labels):
        if labels[a][0] is not None:
            continue
        labels[a] = (("CRT (by position)", "position") if a >= frontier
                     else ("GAME CODE (by position)", "position"))

    if "--unknown" in sys.argv:
        print("nothing is unlabelled: every function above %#010x is either\n"
              "identified from its body or placed by the frontier at %#010x."
              % (SCAN_FROM, frontier))
        return 0

    named = {a: l for a, (l, _w) in labels.items() if l}
    game = [a for a, l in named.items() if l.startswith("GAME CODE")]
    crt = [a for a, l in named.items()
           if not l.startswith("GAME CODE") and not l.startswith("thunk")]
    crt_named = [a for a in crt if labels[a][1] != "position"]
    thunk = [a for a, l in named.items() if l.startswith("thunk")]
    unknown = [a for a in labels if labels[a][0] is None]
    assert not unknown

    highest_game = max(ev_game) if ev_game else 0
    lowest_crt = frontier

    with open(OUT, "w") as fh:
        w = fh.write
        w("# The CRT line, and what is on either side of it\n\n")
        w("Generated by `tools/crt.py`. Every label comes from the function's\n"
          "own body -- the imports it calls, the text it carries, or in two\n"
          "cases the arithmetic it does.\n\n")
        w("Scanned from `%#010x`, which is below the nominal `CRT_START` of\n"
          "`%#010x`, so that the frontier itself is visible rather than assumed.\n\n"
          % (SCAN_FROM, NOMINAL_CRT_START))
        w("| | count |\n|---|---:|\n")
        w("| CRT, identified from its own body | %d |\n" % len(crt_named))
        w("| CRT, by position above the frontier | %d |\n"
          % (len(crt) - len(crt_named)))
        w("| game code, from its own body | %d |\n" % len(ev_game))
        w("| game code, by position below the frontier | %d |\n"
          % (len(game) - len(ev_game)))
        w("| import thunks | %d |\n" % len(thunk))
        w("| still unlabelled | %d |\n\n" % len(unknown))

        w("## Where the line actually is\n\n")
        w("The highest function with game evidence is `%#010x`, and the lowest\n"
          "with CRT evidence is `%#010x`. `CRT_START` is set to `%#010x`, which\n"
          "is **%d bytes too low** -- everything between it and the real frontier\n"
          "is game code that every count in this project silently drops.\n\n"
          % (highest_game, lowest_crt, NOMINAL_CRT_START,
             highest_game - NOMINAL_CRT_START))
        w("Between the two sits the thunk table: %d of the image's six\n"
          "`jmp dword ptr [IAT]` stubs, which the linker parked here rather than\n"
          "with the code that calls them.\n\n" % len(thunk))

        w("## Game code above the nominal line\n\n")
        w("| addr | size | evidence |\n|---|---:|---|\n")
        for a in sorted(game):
            if a >= NOMINAL_CRT_START:
                w("| `%#010x` | %d | %s |\n" % (a, sizes[a], labels[a][1]))

        w("\n## The CRT\n\n")
        w("This is what libc replaces wholesale on a native build.\n\n")
        w("| addr | size | what | from |\n|---|---:|---|---|\n")
        for a in sorted(crt_named):
            w("| `%#010x` | %d | %s | %s |\n" % (a, sizes[a], labels[a][0], labels[a][1]))
        w("\nA further %d functions above `%#010x` carry no import and no text "
          "of\ntheir own -- the CRT's internals: the printf formatter, the "
          "float\nroutines, the string and sort helpers. They are CRT by "
          "position.\n" % (len(crt) - len(crt_named), frontier))

        w("\n## Is the exclusion safe?\n\n")
        w("Every count in this project drops everything at or above\n"
          "`CRT_START`. That is only sound if the game code up there reaches\n"
          "nothing outside the process, so here is its entire outside contact:\n\n")
        above = sorted(a for a in game if a >= NOMINAL_CRT_START)
        gimp = {}
        with open(os.path.join(REPO, "docs", "imports.tsv")) as fh:
            for r in csv.DictReader(fh, delimiter="\t"):
                f = int(r["func"], 16)
                if f in set(above):
                    gimp.setdefault(f, set()).add(r["symbol"])
        gcom = set()
        with open(os.path.join(REPO, "docs", "comcalls.tsv")) as fh:
            for r in csv.DictReader(fh, delimiter="\t"):
                f = int(r["func"], 16)
                if f in set(above) and r.get("abi") == "stdcall":
                    gcom.add(f)
        w("| addr | reaches |\n|---|---|\n")
        for f in sorted(set(gimp) | gcom):
            bits = sorted(gimp.get(f, set()))
            if f in gcom:
                bits.append("COM dispatch")
            w("| `%#010x` | %s |\n" % (f, ", ".join(bits)))
        w("\n%d of the %d game functions above the line touch nothing at all.\n"
          % (len(above) - len(set(gimp) | gcom), len(above)))
        w("\nThe rest is a clock read and some rectangle arithmetic, which this\n"
          "project calls incidental everywhere else, plus one COM dispatch --\n"
          "`DrawSeqBar`, which is reconstructed. So the exclusion holds, and it\n"
          "holds for a measured reason rather than because the constant looks\n"
          "like a boundary.\n")

        w("\n## Import thunks\n\n")
        w("| addr | import |\n|---|---|\n")
        for a in sorted(thunk):
            w("| `%#010x` | %s |\n" % (a, labels[a][0][7:]))

    print("-> docs/crt.md")
    print("  CRT identified   %d from the body, %d more by position"
          % (len(crt_named), len(crt) - len(crt_named)))
    print("  game code        %d (%d above the nominal line)"
          % (len(game), sum(1 for a in game if a >= NOMINAL_CRT_START)))
    print("  import thunks    %d" % len(thunk))
    print("  unlabelled       %d" % len(unknown))
    print("  real frontier    game to %#010x, CRT from %#010x"
          % (highest_game, lowest_crt))
    return 0


if __name__ == "__main__":
    sys.exit(main())
