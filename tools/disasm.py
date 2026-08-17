"""Disassemble one function of ArmyMen2.exe, annotated for reconstruction.

    tools/dis.py 0x0041afc0            one function, bounded by functions.tsv
    tools/dis.py 0x0041afc0 --raw 64   64 bytes from an address, unbounded

Every reconstruction here is checked line by line against the disassembly, so
the listing carries the things that are looked up over and over:

  * imported symbols resolved at `call [0x46f0xx]`, from the IAT;
  * COM vtable dispatch flagged with its slot number, since `call [eax+0x64]`
    on its own says nothing;
  * string literals resolved for any immediate that points at one;
  * the epilogue's `ret N` called out, because argument count and calling
    convention are read straight off it and getting that wrong corrupts the
    caller's stack rather than failing to build.
"""

import bisect
import csv
import os
import sys

import capstone

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import am2

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FUNCS = os.path.join(REPO, "docs", "functions.tsv")

OP_IMM = capstone.x86.X86_OP_IMM
OP_MEM = capstone.x86.X86_OP_MEM


def load_functions():
    out = []
    with open(FUNCS) as fh:
        for row in csv.DictReader(fh, delimiter="\t"):
            out.append((int(row["addr"], 16), int(row["size"]), row["tu"]))
    return sorted(out)


def load_imports(img):
    slots = {}
    img.pe.parse_data_directories()
    for entry in getattr(img.pe, "DIRECTORY_ENTRY_IMPORT", []):
        dll = entry.dll.decode("latin-1")
        for imp in entry.imports:
            name = imp.name.decode("latin-1") if imp.name else f"#{imp.ordinal}"
            slots[imp.address] = f"{dll.split('.')[0]}:{name}"
    return slots


def main():
    args = [a for a in sys.argv[1:]]
    if not args:
        print(__doc__)
        return 2
    addr = int(args[0], 16)
    raw = None
    if "--raw" in args:
        raw = int(args[args.index("--raw") + 1], 0)

    img = am2.Image()
    imports = load_imports(img)
    funcs = load_functions()
    addrs = [f[0] for f in funcs]

    if raw is not None:
        start, size, tu = addr, raw, "?"
    else:
        i = bisect.bisect_right(addrs, addr) - 1
        if i < 0 or addr >= funcs[i][0] + funcs[i][1]:
            print(f"{addr:#x} is not inside a known function; use --raw N")
            return 1
        start, size, tu = funcs[i]

    callers = "?"
    with open(FUNCS) as fh:
        for row in csv.DictReader(fh, delimiter="\t"):
            if int(row["addr"], 16) == start:
                callers = row["callers"]
                break
    print(f"; 0x{start:08x}  {size} bytes  {callers} direct callers  [{tu}]\n")

    data = img.read(start, size)
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True

    off = 0
    while off < size:
        got = False
        for insn in md.disasm(data[off:], start + off):
            got = True
            off = insn.address - start + insn.size
            note = ""

            for op in insn.operands:
                if op.type == OP_MEM and op.mem.base == 0 and op.mem.index == 0:
                    slot = op.mem.disp & 0xFFFFFFFF
                    if slot in imports:
                        note = f"   ; {imports[slot]}"
                elif op.type == OP_MEM and op.mem.base != 0 and op.mem.index == 0:
                    if insn.mnemonic == "call" and op.mem.disp >= 0 \
                            and op.mem.disp % 4 == 0:
                        note = f"   ; vtable slot {op.mem.disp // 4}"
                elif op.type == OP_IMM:
                    s = img.cstring(op.imm) if img.valid(op.imm) else None
                    if s and len(s) > 2:
                        esc = s.replace("\n", "\\n").replace("\r", "\\r")
                        note = f'   ; "{esc[:60]}"'

            if insn.mnemonic.startswith("ret") and insn.op_str:
                note += f"   <<< cleans {insn.op_str} bytes of arguments"

            print(f"  0x{insn.address:08x}  {insn.mnemonic:<7} {insn.op_str}{note}")
        if not got:
            print(f"  0x{start + off:08x}  .byte 0x{data[off]:02x}")
            off += 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
