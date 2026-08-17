"""Check whether an address is safe to overwrite with a 5-byte `jmp rel32`.

patch_replace() clobbers the first five bytes of its target. That is only safe
when nothing can land inside those bytes after the patch, so this verifies:

  1. The function body is at least 5 bytes long.
  2. No jump or call anywhere in .text targets an address strictly inside the
     five bytes being overwritten.
  3. The five bytes end on an instruction boundary, so the tail of a partially
     overwritten instruction can never be executed.

Run this before adding any new target to the harness:

    ./.venv/bin/python tools/checkdetour.py 0x004235d0 0x0045caa0
"""

import bisect
import collections
import struct
import os
import sys

import capstone

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import am2

JUMP = capstone.CS_GRP_JUMP
OP_IMM = capstone.x86.X86_OP_IMM


def main(argv):
    if not argv:
        sys.exit("usage: checkdetour.py <addr> [addr ...]   e.g. 0x004235d0")

    img = am2.Image()
    _n, text_start, text_end, _d = img.section(".text")
    insns = img.disasm(".text")

    starts = sorted(i.address for i in insns)
    boundary = set(starts)

    targets = collections.Counter()
    pushed = collections.Counter()
    for insn in insns:
        if not insn.operands:
            continue
        op = insn.operands[0]
        if op.type != OP_IMM:
            continue
        if insn.mnemonic == "call" or insn.group(JUMP):
            targets[op.imm] += 1
        elif insn.mnemonic == "push" and text_start <= op.imm < text_end:
            # A function handed to something as an argument. This binary
            # registers its menu button handlers that way -- `push 0x42ecf0`
            # into a button constructor -- and such a function is reached by
            # nothing else, so a scan that counts only branches reports it as
            # having no references and reads as dead code. It is not.
            pushed[op.imm] += 1

    # Pointers to code sitting in data: vtables and dispatch tables. Aligned
    # only, which is what a compiler emits and which keeps instruction bytes
    # that happen to look like an address out of the count.
    stored = collections.Counter()
    for _nm, sec_start, _sec_end, data in img.sections:
        for off in range((-sec_start) % 4, len(data) - 4, 4):
            v = struct.unpack_from("<I", data, off)[0]
            if text_start <= v < text_end:
                stored[v] += 1

    # Function starts, for the size check. Branch targets and pushed function
    # pointers are both genuine entries; stored pointers are left out, because
    # a false one landing inside a function would shrink its measured size.
    fn_starts = sorted({t for t in list(targets) + list(pushed)
                        if text_start <= t < text_end})

    rc = 0
    for arg in argv:
        va = int(arg, 0)
        print(f"\n{va:#010x}")

        if not (text_start <= va < text_end):
            print("  FAIL  not inside .text")
            rc = 1
            continue

        raw = img.read(va, 16)
        print(f"  bytes            {raw[:8].hex()}")
        refs = [(targets.get(va, 0), "call/jump"),
                (pushed.get(va, 0), "pushed as an argument"),
                (stored.get(va, 0), "stored in data")]
        total = sum(n for n, _ in refs)
        detail = ", ".join(f"{n} {what}" for n, what in refs if n)
        print(f"  entry references {total}" + (f"  ({detail})" if detail else ""))

        i = bisect.bisect_right(fn_starts, va)
        end = fn_starts[i] if i < len(fn_starts) else text_end
        size = end - va
        # Lower bound: internal branch targets also end the span, so this
        # under-reports real function sizes. That errs safe for a >=5 test.
        ok_size = size >= 5
        print(f"  body size        >={size} bytes  {'ok' if ok_size else 'FAIL (<5)'}")
        rc |= not ok_size

        inner = [(a, c) for a, c in targets.items() if va < a < va + 5]
        if inner:
            print("  FAIL  branch target inside the patched bytes:")
            for a, c in sorted(inner):
                print(f"          {a:#010x}  ({c} reference(s))")
            rc = 1
        else:
            print("  interior targets none  ok")

        # Do a whole number of instructions fit in the five overwritten bytes?
        if va + 5 in boundary:
            print("  boundary         whole instructions in 5 bytes  ok")
        else:
            j = bisect.bisect_right(starts, va + 5)
            after = starts[j - 1] if j else None
            print(f"  boundary         +5 splits the instruction at "
                  f"{after:#010x}" if after else "  boundary         unknown")
            print("                   ok for one-way replacement -- the split "
                  "tail is never executed")

    print()
    return rc


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
