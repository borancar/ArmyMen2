#!/usr/bin/env python3
"""How many game functions are still original, measured rather than remembered.

Every hand-kept count of this in the project has gone stale, and three
separate methods have been wrong in ways that got WORSE as the work
finished.  The failures are all recorded in STATUS.md; this tool exists so
the number is regenerated instead of quoted.

Two corrections are what make it right, and both were found by measuring:

  * SPLIT MERGED ENTRIES.  docs/functions.tsv runs neighbours together
    where it cannot see a boundary, so counting its entries credits a
    whole merged entry the moment any one function inside it is patched.
    tools/merges.py splits them at referenced addresses.

  * CONTAIN, DO NOT MATCH ON START.  ADDR_WND_PROC is 0x0040A6B0 inside an
    entry beginning 0x0040A6A0, so an exact-start match reports 2,256
    bytes of finished work as outstanding.

It also separates the C++ STATIC INITIALIZERS -- the null-terminated
function-pointer array at 0x00473004 that _initterm runs, plus the 16-byte
incremental-linking thunks that jmp into them.  Those are real and
reachable (the launcher injects into a suspended process, so install()
completes before the EXE entry point), but they are a different KIND of
work from game logic and burying them in one total hides that.
"""
import csv, os, struct, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

import am2, capstone, merges


def remaining():
    img = am2.Image()
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    splits = merges.real_functions(img)
    done = set(merges.reconstructed())

    funcs = []
    with open(os.path.join(REPO, "docs", "functions.tsv")) as fh:
        for row in csv.DictReader(fh, delimiter="\t"):
            addr, size = int(row["addr"], 16), int(row["size"])
            if addr >= merges.CRT_START:
                continue
            if addr in splits:
                starts, total = splits[addr]
                starts = sorted(set(starts))
                for i, st in enumerate(starts):
                    end = starts[i + 1] if i + 1 < len(starts) else addr + total
                    funcs.append((st, end - st))
            else:
                funcs.append((addr, size))

    rem = [(a, s) for a, s in funcs
           if not any(a <= d < a + s for d in done)]

    # the CRT static-initializer table and the thunks that jmp into it
    init = set(p for p in struct.unpack("<21I", img.read(0x00473004, 84)) if p)
    for p in list(init):
        ins = list(md.disasm(img.read(p, 8), p))
        if ins and ins[0].mnemonic == "jmp":
            init.add(int(ins[0].op_str, 16))

    # Jump tables sit in .text and are counted as functions by any tool that
    # trusts docs/functions.tsv.  Two were found by hand: 0x00427974 is the
    # target of `jmp dword ptr [ecx*4 + 0x427974]` and disassembles to
    # nonsense.  An entry whose every dword is either a .text address or
    # MSVC's 0x90909090 padding is data, not code.
    # Two shapes.  A pure table is all .text addresses or MSVC padding.  A
    # MIXED one -- 0x004263C8 and 0x004162D8 -- is a run of addresses followed
    # by the switch's byte index table, so the all-dwords test misses it; two
    # leading .text addresses is the tell, and a real function is vanishingly
    # unlikely to open with eight bytes that both read as code pointers.
    tables = set()
    for a, size in rem:
        if size % 4 or size > 256:
            continue
        words = struct.unpack("<%dI" % (size // 4), img.read(a, size))
        def is_text(w):
            return 0x00401000 <= w < merges.CRT_START

        if words and all(is_text(w) or w in (0, 0x90909090) for w in words) \
           and any(is_text(w) for w in words):
            tables.add(a)
        elif len(words) >= 2 and is_text(words[0]) and is_text(words[1]):
            tables.add(a)

    return funcs, rem, init, tables


def main():
    funcs, rem, init, tables = remaining()
    ini  = [(a, s) for a, s in rem if a in init]
    tab  = [(a, s) for a, s in rem if a not in init and a in tables]
    game = [(a, s) for a, s in rem if a not in init and a not in tables]

    print("real functions below the CRT line: %d" % len(funcs))
    print("still original                   : %d functions, %d bytes"
          % (len(rem), sum(s for _, s in rem)))
    print("  C++ static initializers        : %d functions, %d bytes"
          % (len(ini), sum(s for _, s in ini)))
    print("  jump tables (data, not code)   : %d entries,   %d bytes"
          % (len(tab), sum(s for _, s in tab)))
    print("  game functions                 : %d functions, %d bytes"
          % (len(game), sum(s for _, s in game)))
    if "-v" in sys.argv:
        print()
        for a, s in sorted(game, key=lambda x: -x[1]):
            print("  0x%08X %5d B" % (a, s))


if __name__ == "__main__":
    main()
