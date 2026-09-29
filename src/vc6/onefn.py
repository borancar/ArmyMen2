#!/usr/bin/env python3
"""Compile one source with the hosted VC6 (relink flags) and show one function
beside the shipped original, instruction by instruction.

    src/vc6/onefn.py SRC FUNC [--diff] [--extra FLAGS]

FUNC is a manifest name (`LabelDraw`, `BuildHudWidgets`) or a constructor /
destructor as ??0Class / ??1Class. `*` marks a differing line; with --diff
only those are printed, and lines that differ only in a relocated slot (a
call target, an address the linker fills) are dropped, so an empty --diff
means the function matches as far as a single compile can tell -- the relink
(`make vc6-relink`) is what resolves those slots for real.

SRC may be a scratch copy (e.g. `_perm_123.cxx` beside the real source, which
the Makefile and the manifest ignore); FUNC is still looked up by name.

This is the inner loop of byte-matching: seconds per try, where the relink
takes minutes. See permute.py for when reading stops helping.
"""
import argparse
import os
import re
import sys
import tempfile

import capstone

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bytematch as BM
import relink as R
sys.path.insert(0, os.path.join(BM.REPO, "tools"))
import am2


def compare(src, fn, extra=()):
    """[(ours, orig)] instruction text pairs, or raise."""
    man = {f: va for _, f, va in BM.manifest()}
    va = man[fn]
    od = tempfile.mkdtemp()
    obj = BM.compile_file(os.path.abspath(src), od,
                          extra=["/DAM2_STANDALONE", "/DAM2_RELINK"] + list(extra))
    if not obj:
        raise RuntimeError("compile failed: " + src)
    secs, syms = BM.parse_coff(obj)
    s = R.resolve_member_first(syms, fn)
    code, _rel = BM.func_bytes(secs, syms, s)
    code = BM.trim(code)
    orig = am2.Image().read(va, len(code))
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    a = ["%s %s" % (i.mnemonic, i.op_str) for i in md.disasm(bytes(code), va)]
    b = ["%s %s" % (i.mnemonic, i.op_str) for i in md.disasm(bytes(orig), va)]
    n = max(len(a), len(b))
    return [(a[i] if i < len(a) else "", b[i] if i < len(b) else "") for i in range(n)]


NUM = re.compile(r"0x[0-9a-f]+|\b\d+\b")


def reloc_only(x, y):
    """Same instruction but for address operands: where ours has a number
    (unrelocated, often 0) the original has an image address. A call or jump
    target always counts -- ours are relative to the object."""
    if not x or not y or x.split()[0] != y.split()[0]:
        return False
    if x.split()[0] in ("call", "jmp") or x.split()[0].startswith("j"):
        return True
    xs, ys = NUM.split(x), NUM.split(y)
    xn, yn = NUM.findall(x), NUM.findall(y)
    if xs != ys or len(xn) != len(yn):
        return False
    return all(a == b or int(b, 0) >= 0x400000 for a, b in zip(xn, yn))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("src")
    ap.add_argument("func")
    ap.add_argument("--diff", action="store_true")
    ap.add_argument("--extra", default="")
    a = ap.parse_args()
    pairs = compare(a.src, a.func, a.extra.split())
    for x, y in pairs:
        same = x == y
        if a.diff and (same or reloc_only(x, y)):
            continue
        print("%s %-40s | %s" % (" " if same else "*", x, y))


if __name__ == "__main__":
    main()
