"""Build the function inventory for ArmyMen2.exe.

Function starts come from three sources, most trustworthy first:

  1. Targets of direct `call rel32` -- if something is called, it is a function.
  2. The PE entry point.
  3. `push ebp; mov ebp, esp` prologues that sit right after a ret/jmp or
     alignment padding. This catches functions only ever reached indirectly
     (vtables, callbacks, function-pointer tables).

Each function then runs to the next start. Translation units are attributed
using docs/savetags.tsv: the linker consumed the .obj files in alphabetical
order, so a function's address bounds it between two known filenames.

Writes docs/functions.tsv.
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
OUT = os.path.join(REPO, "docs", "functions.tsv")
ANCHORS = os.path.join(REPO, "docs", "savetags.tsv")

OP_IMM = capstone.x86.X86_OP_IMM
# push ebp ; mov ebp, esp  -- the standard MSVC 6 frame prologue.
PROLOGUE = b"\x55\x8b\xec"
# Bytes MSVC 6 pads between functions with.
PAD = {0x90, 0xCC}


def load_anchors():
    """[(addr, filename)] sorted by address, from the CheckSaveTag call sites."""
    if not os.path.exists(ANCHORS):
        return []
    out = []
    with open(ANCHORS) as fh:
        for row in csv.DictReader(fh, delimiter="\t"):
            if row["file"] == "?":
                continue
            out.append((int(row["addr"], 16), row["file"]))
    return sorted(out)


def attribute(addr, anchors):
    """Bound a function between the nearest known filenames above and below."""
    if not anchors:
        return "?"
    addrs = [a for a, _f in anchors]
    i = bisect.bisect_right(addrs, addr)
    below = anchors[i - 1][1] if i > 0 else None
    above = anchors[i][1] if i < len(anchors) else None
    if below and above:
        return below if below == above else f"{below}..{above}"
    if below:
        return f">={below}"
    return f"<={above}"


def main():
    img = am2.Image()
    _n, text_start, text_end, data = img.section(".text")
    insns = img.disasm(".text")
    print(f"{len(insns)} instructions decoded in .text "
          f"({text_start:#x}-{text_end:#x}, {text_end - text_start} bytes)")

    # 1. direct call targets
    callers = collections.Counter()
    for insn in insns:
        if insn.mnemonic != "call" or not insn.operands:
            continue
        op = insn.operands[0]
        if op.type == OP_IMM and text_start <= op.imm < text_end:
            callers[op.imm] += 1
    starts = set(callers)
    print(f"  {len(starts)} distinct direct-call targets")

    # 2. entry point
    entry = img.base + img.pe.OPTIONAL_HEADER.AddressOfEntryPoint
    starts.add(entry)

    # 3. prologues that follow a terminator or padding
    found = 0
    at = 0
    while True:
        at = data.find(PROLOGUE, at)
        if at < 0:
            break
        va = text_start + at
        prev = data[at - 1] if at else 0xCC
        if prev in PAD or prev == 0xC3 or prev == 0xC2:
            if va not in starts:
                starts.add(va)
                found += 1
        at += 1
    print(f"  {found} extra functions from isolated prologues")

    starts = sorted(s for s in starts if text_start <= s < text_end)
    anchors = load_anchors()

    rows = []
    for i, s in enumerate(starts):
        end = starts[i + 1] if i + 1 < len(starts) else text_end
        rows.append((s, end - s, callers.get(s, 0), attribute(s, anchors)))

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w") as fh:
        fh.write("addr\tsize\tcallers\ttu\n")
        for addr, size, ncall, tu in rows:
            fh.write(f"0x{addr:08x}\t{size}\t{ncall}\t{tu}\n")

    total = sum(r[1] for r in rows)
    print(f"\n{len(rows)} functions, {total} bytes covered "
          f"({100.0 * total / (text_end - text_start):.1f}% of .text)"
          f" -> {os.path.relpath(OUT, REPO)}")

    print("\nmost-called functions (likely shared helpers):")
    for addr, size, ncall, tu in sorted(rows, key=lambda r: -r[2])[:15]:
        print(f"  0x{addr:08x}  {size:>6} bytes  {ncall:>4} callers   {tu}")

    print("\nlargest functions:")
    for addr, size, ncall, tu in sorted(rows, key=lambda r: -r[1])[:15]:
        print(f"  0x{addr:08x}  {size:>6} bytes  {ncall:>4} callers   {tu}")

    print("\nfunctions per translation-unit band:")
    per = collections.Counter(r[3] for r in rows)
    for tu, n in sorted(per.items(), key=lambda kv: -kv[1]):
        print(f"  {n:>5}  {tu}")


if __name__ == "__main__":
    main()
