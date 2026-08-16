"""Harvest the debug log messages left behind in ArmyMen2.exe.

The logger at 0x45caa0 is a single `ret` -- the retail build stubbed the body
out. But it was stubbed at the *callee*, not the call sites, so all ~617 calls
still push their real format strings and arguments. The strings survive intact
in .data.

This is the densest source of naming information in the binary: the messages
name subsystems, struct fields, states and error conditions in the developers'
own words, each pinned to an address we can attribute to a translation unit.

Writes docs/logs.tsv.
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
OUT = os.path.join(REPO, "docs", "logs.tsv")

LOGGER = 0x45CAA0
OP_IMM = capstone.x86.X86_OP_IMM


def load_tu_index():
    path = os.path.join(REPO, "docs", "functions.tsv")
    if not os.path.exists(path):
        return [], []
    addrs, tus = [], []
    with open(path) as fh:
        for row in csv.DictReader(fh, delimiter="\t"):
            addrs.append(int(row["addr"], 16))
            tus.append(row["tu"])
    return addrs, tus


def main():
    img = am2.Image()
    insns = img.disasm(".text")
    by_addr = {i.address: n for n, i in enumerate(insns)}
    fn_addrs, fn_tus = load_tu_index()

    rows = []
    for insn in insns:
        if insn.mnemonic != "call" or not insn.operands:
            continue
        op = insn.operands[0]
        if op.type != OP_IMM or op.imm != LOGGER:
            continue
        # Walk back over the pushed arguments; the format string is the last
        # one pushed (cdecl, right-to-left), i.e. the first one we meet.
        idx = by_addr[insn.address]
        fmt = None
        for k in range(idx - 1, max(idx - 12, -1), -1):
            back = insns[k]
            if back.mnemonic != "push":
                if back.mnemonic in ("mov", "lea", "xor", "add", "sub", "test", "cmp"):
                    continue
                break
            o = back.operands[0]
            if o.type == OP_IMM and img.valid(o.imm):
                s = img.cstring(o.imm)
                if s:
                    fmt = s
                    break
        if fmt is None:
            continue
        i = bisect.bisect_right(fn_addrs, insn.address) - 1
        rows.append((insn.address,
                     fn_addrs[i] if i >= 0 else 0,
                     fn_tus[i] if i >= 0 else "?",
                     fmt))

    rows.sort()
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w") as fh:
        fh.write("addr\tfunc\ttu\tmessage\n")
        for addr, fn, tu, fmt in rows:
            fh.write(f"0x{addr:08x}\t0x{fn:08x}\t{tu}\t{fmt.rstrip()}\n")

    print(f"{len(rows)} log call sites with recovered format strings "
          f"-> {os.path.relpath(OUT, REPO)}")
    print(f"{len(set(r[1] for r in rows))} distinct functions contain logging\n")

    per = collections.Counter(r[2] for r in rows)
    print("messages per translation-unit band:")
    for tu, n in sorted(per.items(), key=lambda kv: -kv[1]):
        print(f"  {n:>4}  {tu}")

    print("\nsample messages:")
    for addr, fn, tu, fmt in rows[:40]:
        print(f"  0x{addr:08x}  [{tu}]  {fmt.rstrip()[:88]}")


if __name__ == "__main__":
    main()
