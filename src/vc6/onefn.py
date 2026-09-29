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

--syms names the symbol behind each relocated slot of ours (`; _am2_x`), which
is what a commutative operand order follows: two loads that differ only in
which global comes first look identical to --diff, and only the relink shows
them apart unless the names are printed.

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


def reloc_symbols(obj, sym):
    """{offset within the function: target symbol name} for one function."""
    import struct
    d = open(obj, "rb").read()
    nsec = struct.unpack_from("<H", d, 2)[0]
    symptr, nsym = struct.unpack_from("<II", d, 8)
    secoff = 20 + struct.unpack_from("<H", d, 16)[0]
    strtab = symptr + nsym * 18
    names = {}
    i = 0
    while i < nsym:
        b = symptr + i * 18
        raw = d[b:b+8]
        if raw[:4] == b"\0\0\0\0":
            o = strtab + struct.unpack_from("<I", d, b+4)[0]
            names[i] = d[o:d.index(b"\0", o)].decode("latin1")
        else:
            names[i] = raw.split(b"\0")[0].decode("latin1")
        i += 1 + d[b+17]
    secs, syms = BM.parse_coff(obj)
    secnum, value = syms[sym]
    b = secoff + (secnum - 1) * 40
    relptr, nrel = struct.unpack_from("<I", d, b+24)[0], struct.unpack_from("<H", d, b+32)[0]
    out = {}
    for r in range(nrel):
        va, idx, _typ = struct.unpack_from("<IIH", d, relptr + r * 10)
        out[va - value] = names.get(idx, "?")
    return out


def compare(src, fn, extra=(), syms_out=None):
    """[(ours, orig)] instruction text pairs, or raise. With syms_out (a
    list), each of ours is annotated with the symbols its relocations name."""
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
    orig = BM.RetailImage().read(va, len(code))
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    rs = reloc_symbols(obj, s) if syms_out is not None else {}
    a = []
    for i in md.disasm(bytes(code), va):
        t = "%s %s" % (i.mnemonic, i.op_str)
        hit = [n for o, n in sorted(rs.items())
               if i.address - va <= o < i.address - va + i.size]
        if hit:
            syms_out.append((len(a), " ; " + " ".join(hit)))
        a.append(t)
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
    # a relocated displacement: ours has none (or 0), the original an address
    xd = re.sub(r" \+ 0\]", "]", x)
    yd = re.sub(r" \+ (0x[0-9a-f]+)\]",
                lambda m: "]" if int(m.group(1), 16) >= 0x400000 else m.group(0), y)
    if xd == yd:
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
    ap.add_argument("--syms", action="store_true")
    a = ap.parse_args()
    notes = [] if a.syms else None
    pairs = compare(a.src, a.func, a.extra.split(), notes)
    note = dict(notes or [])
    for k, (x, y) in enumerate(pairs):
        same = x == y
        if a.diff and (same or reloc_only(x, y)):
            continue
        print("%s %-40s | %s%s" % (" " if same else "*", x, y, note.get(k, "")))


if __name__ == "__main__":
    main()
