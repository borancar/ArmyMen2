"""Map the Win32 / DirectX boundary of ArmyMen2.exe.

The port has to take over every call that leaves the process. This finds them:
each imported symbol's IAT slot, every instruction in .text that reaches that
slot, and the function the instruction sits in.

Two shapes reach an import, and both matter:

  1. `call dword ptr [slot]`  -- the ordinary case, and `jmp dword ptr [slot]`
     for a tail call.
  2. `mov reg, dword ptr [slot]` followed by an indirect `call reg` somewhere
     later. MSVC 6 does this when a call sits in a loop. Counting only form 1
     would under-report the boundary, so both are recorded and the `how`
     column says which was seen.

A function is at the boundary if it contains any such site. That set -- not the
import count -- is what has to be reconstructed before the game can stop being
hosted by Wine.

Writes docs/imports.tsv.
"""

import bisect
import collections
import csv
import os
import sys

import capstone

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import am2

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(REPO, "docs", "imports.tsv")
FUNCS = os.path.join(REPO, "docs", "functions.tsv")

OP_MEM = capstone.x86.X86_OP_MEM
OP_REG = capstone.x86.X86_OP_REG

# The statically linked MSVC 6 CRT starts around here; it reaches plenty of
# kernel32 itself, but it is replaced wholesale by libc rather than ported
# function by function, so it is reported separately.
CRT_START = 0x0045C000


def load_functions():
    """[(addr, size, tu)] sorted by address."""
    out = []
    with open(FUNCS) as fh:
        for row in csv.DictReader(fh, delimiter="\t"):
            out.append((int(row["addr"], 16), int(row["size"]), row["tu"]))
    return sorted(out)


def owner(addr, funcs, addrs):
    """The function containing `addr`, or None."""
    i = bisect.bisect_right(addrs, addr) - 1
    if i < 0:
        return None
    start, size, tu = funcs[i]
    return (start, tu) if addr < start + size else None


def load_imports(img):
    """{iat_slot_va: (dll, symbol)} for every imported function."""
    slots = {}
    img.pe.parse_data_directories()
    for entry in getattr(img.pe, "DIRECTORY_ENTRY_IMPORT", []):
        dll = entry.dll.decode("latin-1")
        for imp in entry.imports:
            if imp.name:
                name = imp.name.decode("latin-1")
            else:
                name = f"#{imp.ordinal}"
            slots[imp.address] = (dll, name)
    return slots


def main():
    img = am2.Image()
    slots = load_imports(img)
    if not slots:
        print("no imports parsed -- wrong file?")
        return 1
    lo, hi = min(slots), max(slots)
    print(f"{len(slots)} imported symbols, IAT {lo:#x}-{hi:#x}")

    funcs = load_functions()
    addrs = [f[0] for f in funcs]

    insns = img.disasm(".text")
    print(f"{len(insns)} instructions decoded in .text")

    # Pass 1: direct call/jmp through a slot, and loads of a slot into a
    # register. A load is only evidence of a *possible* call, but MSVC 6 does
    # not load an import address for any other reason.
    sites = []
    for insn in insns:
        if not insn.operands:
            continue
        op = insn.operands[0]
        if op.type != OP_MEM:
            continue
        # No base or index register: an absolute [disp32] operand.
        if op.mem.base != 0 or op.mem.index != 0:
            continue
        slot = op.mem.disp & 0xFFFFFFFF
        if slot not in slots:
            continue
        if insn.mnemonic in ("call", "jmp"):
            how = insn.mnemonic
        elif insn.mnemonic == "mov" and len(insn.operands) == 2:
            # mov reg, [slot] -- an indirect call is set up.
            if insn.operands[0].type != OP_REG:
                continue
            how = "load"
        else:
            continue
        dll, sym = slots[slot]
        sites.append((insn.address, slot, dll, sym, how))

    print(f"{len(sites)} import reference sites")

    rows = []
    orphan = 0
    for addr, slot, dll, sym, how in sorted(sites):
        own = owner(addr, funcs, addrs)
        if own is None:
            orphan += 1
            fn, tu = 0, "?"
        else:
            fn, tu = own
        rows.append((addr, fn, tu, dll, sym, how))
    if orphan:
        print(f"  ({orphan} sites not inside any known function)")

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w") as fh:
        fh.write("site\tfunc\ttu\tdll\tsymbol\thow\n")
        for addr, fn, tu, dll, sym, how in rows:
            fh.write(f"0x{addr:08x}\t0x{fn:08x}\t{tu}\t{dll}\t{sym}\t{how}\n")

    game = [r for r in rows if r[1] and r[1] < CRT_START]
    crt = [r for r in rows if r[1] >= CRT_START]
    game_fns = {r[1] for r in game}
    crt_fns = {r[1] for r in crt}

    print(f"\n-> {os.path.relpath(OUT, REPO)}")
    print(f"\nboundary functions below the CRT: {len(game_fns)}")
    print(f"  ({len(crt_fns)} more inside the CRT at >={CRT_START:#x}, replaced by libc)")

    print("\nby DLL (game code only):")
    per_dll = collections.Counter(r[3] for r in game)
    for dll, n in per_dll.most_common():
        fns = len({r[1] for r in game if r[3] == dll})
        print(f"  {dll:<16} {n:>4} sites  {fns:>3} functions")

    print("\nmost-used imported symbols (game code only):")
    per_sym = collections.Counter(f"{r[3]}:{r[4]}" for r in game)
    for sym, n in per_sym.most_common(20):
        print(f"  {n:>4}  {sym}")

    print("\nboundary functions with the most import sites:")
    per_fn = collections.Counter(r[1] for r in game)
    size_of = {a: s for a, s, _t in funcs}
    for fn, n in per_fn.most_common(25):
        syms = sorted({r[4] for r in game if r[1] == fn})
        tu = next(r[2] for r in game if r[1] == fn)
        shown = ", ".join(syms[:5]) + (" ..." if len(syms) > 5 else "")
        print(f"  0x{fn:08x}  {size_of.get(fn, 0):>5}B  {n:>3} sites  [{tu}]  {shown}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
