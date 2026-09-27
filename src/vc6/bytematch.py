#!/usr/bin/env python3
"""Recompile the reconstruction with the hosted MSVC 6.0 and byte-match it
against the shipped ArmyMen2.exe, one function at a time.

This is the point of the whole `src/vc6` host (see README.md): the reconstruction
derives its logic from the disassembly, and the strongest possible confirmation
that a function is right is that the *original compiler* turns our source back
into the exact bytes the game shipped.  A match is proof; a diff is a lead --
usually the reconstruction is correct but written in a form MSVC 6 compiles
differently (a 32-bit expression where the original worked in 16-bit words, a
temporary that lands in a different register), which is a source-shape question,
not a behaviour one.

METHOD.  For every address a `patch_replace(ADDR_*, (const void*)Fn, ...)` call
installs over, compile the owning source with `build/vc6host` + the real
`CL.EXE` at /O2, pull the function's bytes and COFF relocations out of the .obj,
and compare to the original at its VA.  A 4-byte slot named by a relocation is
an address the linker has not filled in yet (0 in the .obj, a real VA in the
binary), so those slots are wildcarded; every other byte must be identical.
Trailing 0x90/0xCC padding is trimmed from both.

SCOPE.  Almost every reconstructed TU compiles under the host now, including
`src/game/win32/*` (the shim grew ole32!CLSIDFromString, DWORD_PTR, a
static_assert strip and a forward LPDIRECTPLAYLOBBY3A to get there).  The one
holdout is dplay.cpp, which calls IDirectPlayLobby3 methods VC98's DirectX 6.0
headers don't declare -- reported nocompile.  crt.cpp (POSIX) and trig.cpp (GCC
`__asm__`) are GCC-only and SKIP.  Note the win32 layer is mostly thiscall
member functions, which VC6 cannot spell (no __thiscall keyword) and so cannot
match -- expect a low match rate there even though it compiles.

This is a REPORT, not a gate: it needs the external VC6 install and prints
coverage.  Run it with `make vc6-match` (which sets VC6_ROOT), or directly.

    src/vc6/bytematch.py                 # coverage summary + per-file tally
    src/vc6/bytematch.py --file dist     # per-function detail for one module
    src/vc6/bytematch.py --diffs         # list every current diff
"""
import argparse
import collections
import os
import re
import struct
import subprocess
import sys
import tempfile

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(REPO, "tools"))
import am2

VC6_ROOT = os.environ.get("VC6_ROOT", "/home/boran/claude/en_vs6_ent")
HOST     = os.path.join(REPO, "build", "vc6host")
CL       = os.path.join(VC6_ROOT, "VC98", "BIN", "CL.EXE")
SHIM     = os.path.join(REPO, "src", "vc6", "include")

# GCC-only sources: not byte-match targets under MSVC6.
SKIP_FILE = {"crt.cpp", "trig.cpp"}

# ---- COFF ---------------------------------------------------------------

def parse_coff(path):
    d = open(path, "rb").read()
    nsec = struct.unpack_from("<H", d, 2)[0]
    symptr, nsym = struct.unpack_from("<II", d, 8)
    optlen = struct.unpack_from("<H", d, 16)[0]
    secoff = 20 + optlen
    strtab = symptr + nsym * 18
    def strn(off):
        e = d.index(b"\0", strtab + off)
        return d[strtab + off:e].decode("latin1")
    secs = []
    for i in range(nsec):
        b = secoff + i * 40
        raw = d[b:b+8]
        name = strn(int(raw[1:].split(b"\0")[0])) if raw[0:1] == b"/" \
               else raw.split(b"\0")[0].decode("latin1")
        _, _, sz, dataptr, relptr, _, nrel, _, _ = \
            struct.unpack_from("<IIIIIIHHI", d, b + 8)
        data = d[dataptr:dataptr+sz] if dataptr else b""
        rels = [struct.unpack_from("<IIH", d, relptr + r * 10)[:3]
                for r in range(nrel)]
        secs.append(dict(data=data, rels=[(va, typ) for va, _, typ in rels]))
    syms = {}
    i = 0
    while i < nsym:
        b = symptr + i * 18
        raw = d[b:b+8]
        name = strn(struct.unpack_from("<I", d, b+4)[0]) if raw[:4] == b"\0\0\0\0" \
               else raw.split(b"\0")[0].decode("latin1")
        _, secnum = struct.unpack_from("<ih", d, b+8)
        aux = d[b+17]
        syms.setdefault(name, (secnum, struct.unpack_from("<i", d, b+8)[0]))
        i += 1 + aux
    return secs, syms

def resolve_sym(syms, func):
    if "_" + func in syms:
        return "_" + func
    # __stdcall decorates as _name@bytes, __fastcall as @name@bytes.
    for pre in ("_", "@"):
        p = pre + func + "@"
        for s in syms:
            if s.startswith(p) and s[len(p):].isdigit():
                return s
    for s in syms:                      # C++ mangled ?name@@...
        if s.startswith("?" + func + "@"):
            return s
    return None

def func_bytes(secs, syms, sym):
    secnum, value = syms[sym]
    if secnum <= 0:
        return None
    sec = secs[secnum - 1]
    data = sec["data"]
    end = len(data)
    for (sn, v) in syms.values():
        if sn == secnum and v > value:
            end = min(end, v)
    code = bytearray(data[value:end])
    relocs = [va - value for (va, typ) in sec["rels"] if value <= va < end]
    return code, relocs

def trim(b):
    b = bytearray(b)
    while b and b[-1] in (0x90, 0xCC):
        b.pop()
    return b

def mask(b, relocs, n):
    b = bytearray(b)
    for off in relocs:
        for k in range(4):
            if off + k < n:
                b[off + k] = 0
    return b

# ---- manifest -----------------------------------------------------------

def manifest():
    addr = {}
    pat = re.compile(r"#define\s+(ADDR_[A-Z0-9_]+)\s+0x([0-9A-Fa-f]+)u?")
    for line in open(os.path.join(REPO, "src/inject/orig.h")):
        m = pat.match(line.strip())
        if m:
            addr[m.group(1)] = int(m.group(2), 16)
    # Two install spellings: a plain function cast `(const void *)Foo`, and a
    # thiscall method installed by address `pmf_addr(&Class::Foo)`.  Capture the
    # (method) name either way; resolve_sym finds the member's mangled symbol.
    call = re.compile(
        r'patch_replace\(\s*(ADDR_[A-Z0-9_]+)\s*,\s*'
        r'(?:\(const void \*\)\s*|pmf_addr\(&(?:[A-Za-z_][A-Za-z0-9_]*::)?)'
        r'([A-Za-z_][A-Za-z0-9_]*)')
    out = []
    for path in am2.game_sources():
        for m in call.finditer(open(path).read()):
            if m.group(1) in addr:
                out.append((os.path.relpath(path, REPO), m.group(2),
                            addr[m.group(1)]))
    return out

# ---- compile ------------------------------------------------------------

def compile_file(relpath, outdir):
    src = os.path.join(REPO, relpath)
    obj = os.path.join(outdir, os.path.basename(relpath)[:-4] + ".obj")
    env = dict(os.environ)
    env["INCLUDE"] = SHIM + ";" + os.path.join(VC6_ROOT, "VC98", "INCLUDE")
    try:
        os.remove(obj)
    except OSError:
        pass
    subprocess.run([HOST, CL, "/O2", "/c", "/Fo" + obj, os.path.basename(src)],
                   cwd=os.path.dirname(src), env=env,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return obj if os.path.exists(obj) else None

# ---- main ---------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--file", help="only this module (basename stem)")
    ap.add_argument("--diffs", action="store_true", help="list every diff")
    args = ap.parse_args()

    if not os.path.exists(HOST):
        sys.exit("build/vc6host missing -- run `make vc6` first")
    if not os.path.exists(CL):
        sys.exit("VC6 install not at %s -- set VC6_ROOT" % VC6_ROOT)

    man = manifest()
    if args.file:
        man = [e for e in man if args.file in e[0]]
    byfile = collections.defaultdict(list)
    for f, fn, va in man:
        byfile[f].append((fn, va))

    img = am2.Image()
    res = collections.Counter()
    per = {}
    diffs = []
    with tempfile.TemporaryDirectory() as outdir:
        for f in sorted(byfile):
            base = os.path.basename(f)
            if base in SKIP_FILE:
                res["skip"] += len(byfile[f])
                per[f] = ("skip", 0, len(byfile[f]))
                continue
            obj = compile_file(f, outdir)
            if obj is None:
                res["nocompile"] += len(byfile[f])
                per[f] = ("nocompile", 0, len(byfile[f]))
                continue
            secs, syms = parse_coff(obj)
            m = d = 0
            for fn, va in byfile[f]:
                sym = resolve_sym(syms, fn)
                fb = func_bytes(secs, syms, sym) if sym else None
                if fb is None:
                    res["nosym"] += 1
                    continue
                code, relocs = fb
                code = trim(code)
                n = len(code)
                orig = bytearray(img.read(va, n))
                if bytes(mask(code, relocs, n)) == bytes(mask(orig, relocs, n)):
                    res["match"] += 1; m += 1
                else:
                    res["diff"] += 1; d += 1
                    diffs.append((f, fn, va, n, len(relocs)))
            per[f] = ("ok", m, m + d)

    print("=== vc6 byte-match coverage (src/game, /O2) ===")
    for k in ("match", "diff", "nosym", "nocompile", "skip"):
        print("  %-10s %d" % (k, res[k]))
    denom = res["match"] + res["diff"]
    print("  %-10s %d functions;  %d/%d = %.1f%% byte-identical as written" %
          ("total", sum(res.values()), res["match"], denom,
           100.0 * res["match"] / max(1, denom)))
    print("\n=== per module ===")
    for f in sorted(per):
        st, m, t = per[f]
        if st == "ok":
            print("  %-34s %2d/%-2d" % (f, m, t))
        elif st != "skip":
            print("  %-34s %s (%d fns)" % (f, st, t))
    if args.diffs or args.file:
        print("\n=== diffs ===")
        for f, fn, va, n, r in diffs:
            print("  %-24s 0x%08x  %-28s %dB %dr" % (fn, va, f, n, r))

if __name__ == "__main__":
    main()
