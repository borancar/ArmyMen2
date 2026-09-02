#!/usr/bin/env python3
"""Find functions.tsv entries that hold more than one function, by counting rets.

tools/merges.py answers the same question and answers it differently: it
confirms a split only where something REFERENCES the second function, so an
entry whose tail is reached only by falling off the end of a jump table, or
whose caller uses an addressing mode the scan cannot follow, stays invisible.
Its own header says the figure is a lower bound.

0x00460800 is the case that motivated this. It is 192 bytes, it holds a pool
initialiser and that pool's teardown, and merges.py does not list it. Reading
it as one function made two adjacent pools look like they had four functions
and five, which killed a correct positional pairing between them for an hour.

A ret is not proof of a boundary -- a function with several returns has
several -- so this is a REPORT and not a gate, in the same spirit as
tools/checkoffsetuse.py. What it is good for is the question "did I just read
a whole function, or the first one in its entry", which is cheap to ask and
was answered wrong three times in one session.

Padding after the last ret is ignored: int3 and nop runs are the linker's.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import capstone

import am2

CRT_START = 0x0045C000


def main():
    """With no argument, report the worst 20. With an address, answer for it.

    The query form exists because the summary is TRUNCATED, and checking a
    specific entry against a top-20 list reads as "not caught" when it is
    simply not in the first twenty. That was done while testing this tool, on
    the very entry it was written for.
    """
    want = None
    if len(sys.argv) > 1:
        want = int(sys.argv[1], 16)
    img = am2.Image()
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    rows = []
    with open(os.path.join(os.path.dirname(os.path.dirname(
            os.path.abspath(__file__))), "docs", "functions.tsv")) as f:
        for line in f.read().splitlines()[1:]:
            if not line:
                continue
            parts = line.split("\t")
            rows.append((int(parts[0], 16), int(parts[1])))

    multi = []
    for addr, size in rows:
        data = img.read(addr, size)
        if not data:
            continue
        ins = list(md.disasm(data, addr))
        # A ret is not a boundary: CheatLine has 23 and is one function. What
        # marks a boundary is a ret followed -- past any int3/nop padding the
        # linker inserted -- by a PROLOGUE. Requiring that took this from 709
        # entries to a list short enough to read.
        starts = []
        for n, i in enumerate(ins):
            if not i.mnemonic.startswith("ret"):
                continue
            j = n + 1
            while j < len(ins) and ins[j].mnemonic in ("nop", "int3"):
                j += 1
            if j >= len(ins):
                continue
            nxt = ins[j]
            prologue = (
                (nxt.mnemonic == "push" and nxt.op_str in
                 ("ebp", "esi", "edi", "ebx"))
                or (nxt.mnemonic == "sub" and nxt.op_str.startswith("esp,"))
                or (nxt.mnemonic == "mov" and nxt.op_str.startswith("edi, edi")))
            if prologue:
                starts.append(nxt.address)
        if starts:
            multi.append((addr, size, starts))

    if want is not None:
        for addr, size, starts in multi:
            if addr <= want < addr + size:
                print("0x%08X (%d B) looks like %d function(s); starts after "
                      "0x%08X: %s" % (addr, size, len(starts) + 1, addr,
                                      " ".join("0x%08X" % s for s in starts)))
                return 0
        for addr, size in rows:
            if addr <= want < addr + size:
                print("0x%08X (%d B) looks like ONE function." % (addr, size))
                return 0
        print("0x%08X is in no functions.tsv entry." % want)
        return 1

    below = [m for m in multi if m[0] < CRT_START]
    print("%d of %d entries look like more than one function (%d below the CRT line)."
          % (len(multi), len(rows), len(below)))
    print("A ret followed by a prologue. Not proof -- read the body. A report.")
    print()
    print("%-12s %8s %6s  %s" % ("entry", "size", "more", "apparent starts"))
    for addr, size, rets in sorted(multi, key=lambda m: -len(m[2]))[:20]:
        shown = " ".join("0x%08X" % r for r in rets[:4])
        if len(rets) > 4:
            shown += " ..."
        print("0x%08X %8d %6d  %s" % (addr, size, len(rets), shown))
    return 0


if __name__ == "__main__":
    sys.exit(main())
