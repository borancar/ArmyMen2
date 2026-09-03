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

# Where the CRT actually begins, per tools/crt.py -- not the nominal constant.
CRT_REAL = 0x00464420

# Reached only through the harness or the game's own IAT, and reconstructing
# either is a DEFECT rather than progress.  0x0045CAA0 is the retail build's
# stubbed logger, which src/inject/gamelog.c patches to capture output --
# CLAUDE.md records that replacing it with an empty function silenced the log
# and blinded half of tools/ab.sh.  The other three are one-instruction
# `jmp [IAT]` import thunks; DirectInput in particular MUST go through its
# thunk, or our own import would resolve past dinput_hook.c's patch.
OFF_LIMITS = {
    0x0045CAA0,  # ADDR_LOG, stubbed to `ret`, owned by the harness
    0x00463390,  # DirectSoundCreate
    0x00463396,  # DirectDrawCreate
    0x00464410,  # DirectInputCreateA
}


def remaining():
    img = am2.Image()
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    # merges.real_functions() stops at the NOMINAL CRT_START, so entries in
    # the band above it are never split -- and a function sharing an entry
    # with a reconstructed neighbour is then credited whole.  That is the
    # merged-entry error this file already documents, and it recurred here
    # the moment the range was widened: six functions between 0x0045E000 and
    # the real frontier were hidden by it.  Split the whole range instead.
    referenced = merges.referenced_addresses(img)
    splits = merges.real_functions(img)
    with open(os.path.join(REPO, "docs", "functions.tsv")) as fh:
        for row in csv.DictReader(fh, delimiter="\t"):
            addr, size = int(row["addr"], 16), int(row["size"])
            if addr < merges.CRT_START or addr >= CRT_REAL or size < 16:
                continue
            starts = [a for a in merges.split_points(img, addr, size, md)
                      if a in referenced]
            if starts:
                splits[addr] = ([addr] + starts, size)

    done = set(merges.reconstructed())

    funcs = []
    with open(os.path.join(REPO, "docs", "functions.tsv")) as fh:
        for row in csv.DictReader(fh, delimiter="\t"):
            addr, size = int(row["addr"], 16), int(row["size"])
            # tools/crt.py measured the REAL frontier: game code runs to
            # 0x00462600 and the CRT proper starts at 0x00464420, with the
            # import thunks parked between.  CRT_START is 26 KB low, so
            # stopping there would hide 112 game functions.
            if addr >= CRT_REAL:
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
            # CRT_REAL, not merges.CRT_START.  A jump table above the nominal
            # line holds addresses above it too, and testing against the
            # nominal constant rejected every one of them -- 0x0046071C, 45
            # dwords all pointing into 0x004603xx, was counted as a 180-byte
            # game function for exactly that reason.  Second range assumption
            # left behind by widening the range; the first was in
            # merges.real_functions.
            return 0x00401000 <= w < CRT_REAL

        if words and all(is_text(w) or w in (0, 0x90909090) for w in words) \
           and any(is_text(w) for w in words):
            tables.add(a)
        elif len(words) >= 2 and is_text(words[0]) and is_text(words[1]):
            tables.add(a)

    # MSVC's incremental-linking thunks: one `jmp` to the real function and
    # then padding, in a 16-byte slot.  The static-initializer table points at
    # the thunk and the thunk jumps to the body, so patching the body is what
    # the CRT actually reaches -- and a thunk is a LINKER artifact, not a
    # function the original source had.  Counting them as work left to do
    # would mean reconstructing the linker.
    thunks = set()
    for a, size in rem:
        if size > 16:
            continue
        ins = [i for i in md.disasm(img.read(a, size), a) if i.mnemonic != "nop"]
        if len(ins) == 1 and ins[0].mnemonic == "jmp":
            try:
                target = int(ins[0].op_str, 16)
            except ValueError:
                continue
            if 0x00401000 <= target < CRT_REAL:
                thunks.add(a)

    return funcs, rem, init, tables, thunks


def main():
    funcs, rem, init, tables, thunks = remaining()
    off  = [(a, s) for a, s in rem if a in OFF_LIMITS]
    rem  = [(a, s) for a, s in rem if a not in OFF_LIMITS]
    thk  = [(a, s) for a, s in rem if a in thunks]
    ini  = [(a, s) for a, s in rem if a not in thunks and a in init]
    tab  = [(a, s) for a, s in rem if a not in thunks and a not in init
            and a in tables]
    game = [(a, s) for a, s in rem if a not in thunks and a not in init
            and a not in tables]

    print("real functions below the CRT frontier: %d" % len(funcs))
    print("still original                   : %d functions, %d bytes"
          % (len(rem), sum(s for _, s in rem)))
    print("  harness / IAT, must NOT be done : %d entries,   %d bytes"
          % (len(off), sum(s for _, s in off)))
    print("  C++ static initializers        : %d functions, %d bytes"
          % (len(ini), sum(s for _, s in ini)))
    print("  linker thunks (one jmp each)   : %d entries,   %d bytes"
          % (len(thk), sum(s for _, s in thk)))
    print("  jump tables (data, not code)   : %d entries,   %d bytes"
          % (len(tab), sum(s for _, s in tab)))
    print("  game functions                 : %d functions, %d bytes"
          % (len(game), sum(s for _, s in game)))
    if not game and not ini:
        print("\nNothing left to transpose: what remains is build artifacts\n"
              "(linker thunks), data (jump tables), and the harness/IAT\n"
              "entries above, none of which is a function the original\n"
              "source had.")

    if "-v" in sys.argv:
        print()
        for a, s in sorted(game, key=lambda x: -x[1]):
            print("  0x%08X %5d B" % (a, s))


if __name__ == "__main__":
    main()
