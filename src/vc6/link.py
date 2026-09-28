#!/usr/bin/env python3
"""VC6 LINK milestone: link the reconstruction into a valid PE with LINK.EXE.

The whole-image relink (relink.py) only SIMULATES the link.  This drives the
real MSVC6 LINK.EXE (hosted by vc6host, made to run in the vc6host commit) to
produce an actual PE from VC6-compiled objects, the carried data blob, the
real MSVC CRT (LIBC.LIB) and the Win32/DirectX import libs.

Placement note: LINK.EXE has no --section-start, and lays sections
sequentially with all code before all data.  It therefore cannot reproduce the
native build's layout (our code at 0x700000, int3 gap at 0x401000); our code
lands in the original .text VA range instead.  So this image LINKS and is a
valid PE, but does not correctly RUN yet -- the blob's baked code-pointers
still point at original VAs and need a fixups dataset (the deferred next step).
This milestone proves the compile -> blob -> LINK pipeline end to end.
"""
import argparse
import glob
import os
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bytematch as BM
import mkcoff
sys.path.insert(0, os.path.join(REPO, "tools"))
import am2
import pefile

VC6_ROOT = BM.VC6_ROOT
HOST = BM.HOST
LINK = os.path.join(VC6_ROOT, "VC98", "BIN", "LINK.EXE")
LIBDIR = os.path.join(VC6_ROOT, "VC98", "LIB")
OUTDIR = os.path.join(REPO, "build", "vc6sa")

# GCC-only TUs (POSIX / inline asm) and the VC6 header holdout.
SKIP = {"crt.cpp", "trig.cpp", "dplay.cpp"}

IMPORT_LIBS = ["KERNEL32.LIB", "USER32.LIB", "GDI32.LIB", "ADVAPI32.LIB",
               "OLE32.LIB", "DDRAW.LIB", "DSOUND.LIB", "DINPUT.LIB",
               "WINMM.LIB", "WS2_32.LIB", "DXGUID.LIB", "LIBC.LIB"]


def tu_list():
    srcs = []
    for pat in ("src/game/*.cpp", "src/game/win32/*.cpp"):
        srcs += sorted(glob.glob(os.path.join(REPO, pat)))
    return [s for s in srcs if os.path.basename(s) not in SKIP]


def make_blob(path):
    """Full byte-exact data image at its VAs: .origdat (0x46F000..0x48E000,
    rdata+data initialised) and .origbss (0x48E000..0x666000, zero-fill).
    Uncarved, so the blob's own internal pointers stay valid."""
    pe = pefile.PE(am2.EXE)
    base = pe.OPTIONAL_HEADER.ImageBase
    raw = {}
    for s in pe.sections:
        va = base + s.VirtualAddress
        raw[s.Name.rstrip(b"\0").decode()] = (va, s.get_data())
    # .rdata (0x46F000) + .data initialised (0x473000) are contiguous in VA.
    rd_va, rd = raw[".rdata"]
    da_va, da = raw[".data"]
    assert rd_va == 0x0046F000 and da_va == 0x00473000, (hex(rd_va), hex(da_va))
    # rdata raw is padded to 0x4000 (0x46F000..0x473000); data raw is the
    # initialised prefix (0x473000..0x48E000 = 0x1B000).
    origdat = rd[:0x4000] + da[:0x1B000]
    assert len(origdat) == 0x1F000, hex(len(origdat))
    bss_size = 0x00666000 - 0x0048E000
    mkcoff.emit(path, [
        {"name": ".origdat", "chars": mkcoff.DATA, "data": origdat,
         "syms": [("_am2_origdat_base", 0)]},
        {"name": ".origbss", "chars": mkcoff.BSS, "bsssize": bss_size,
         "syms": [("_am2_origbss_base", 0)]},
    ])
    return path


def compile_all():
    os.makedirs(OUTDIR, exist_ok=True)
    objs, fails = [], []
    for s in tu_list():
        rel = os.path.relpath(s, REPO)
        obj = BM.compile_file(rel, OUTDIR)
        (objs if obj else fails).append(obj or rel)
    # glue.c (CRT seam pointers)
    g = BM.compile_file("src/vc6/glue.cpp", OUTDIR)
    if g:
        objs.append(g)
    else:
        fails.append("src/vc6/glue.cpp")
    return objs, fails


def run_link(objs, blob):
    out = os.path.join(OUTDIR, "ArmyMen2.exe")
    try:
        os.remove(out)
    except OSError:
        pass
    # A 49-object + 12-lib command line overruns vc6host's argv reconstruction
    # and trailing libraries silently drop (WINMM's imports went unresolved
    # though the lib was searched).  LINK's @response-file avoids the command
    # line entirely -- the standard fix for a large link -- and also sidesteps
    # the leading-'/' option confusion since paths sit in the file.  cwd is
    # OUTDIR, so objects are basenames.
    resp = os.path.join(OUTDIR, "link.rsp")
    with open(resp, "w") as f:
        f.write("/NOLOGO\n/FIXED\n/BASE:0x400000\n/SUBSYSTEM:WINDOWS\n")
        f.write("/OUT:ArmyMen2.exe\n")
        for o in objs:
            f.write(os.path.basename(o) + "\n")
        f.write(os.path.basename(blob) + "\n")
        f.write("/LIBPATH:\"%s\"\n" % LIBDIR)
        f.write("\n".join(IMPORT_LIBS) + "\n")
    args = [HOST, LINK, "@link.rsp"]
    env = dict(os.environ)
    env["LIB"] = LIBDIR
    env["VC6_ROOT"] = VC6_ROOT
    r = subprocess.run(args, env=env, cwd=OUTDIR, capture_output=True, text=True)
    return out, r


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--unresolved", action="store_true", help="list unresolved symbols only")
    ap.add_argument("--relink", action="store_true", help="reuse existing .objs, only re-link")
    # Ignore stray args: the Makefile's global ARGS (default "-nointro -dbg" for
    # `run`) is passed through to $(ARGS); this is a report tool, not the game.
    args, _ = ap.parse_known_args()
    if not os.path.exists(HOST):
        sys.exit("build/vc6host missing -- run `make vc6`")

    if args.relink:
        objs = [os.path.join(OUTDIR, os.path.basename(s)[:-4] + ".obj") for s in tu_list()]
        objs = [o for o in objs if os.path.exists(o)]
        objs.append(os.path.join(OUTDIR, "glue.obj"))
        print("reusing %d existing objs" % len(objs))
    else:
        print("compiling %d TUs with VC6 CL ..." % len(tu_list()))
        objs, fails = compile_all()
        print("  compiled %d, failed %d" % (len(objs), len(fails)))
        for f in fails:
            print("    FAIL", f)

    blob = make_blob(os.path.join(OUTDIR, "origblob.obj"))
    print("blob: %s" % blob)

    out, r = run_link(objs, blob)
    txt = (r.stdout or "") + (r.stderr or "")
    unresolved = [ln for ln in txt.splitlines() if "LNK2001" in ln or "unresolved" in ln.lower()]
    other = [ln for ln in txt.splitlines()
             if ("error" in ln.lower() or "LNK" in ln) and "LNK2001" not in ln]
    print("LINK rc=%d" % r.returncode)
    if unresolved:
        print("=== %d unresolved ===" % len(unresolved))
        for ln in unresolved[:80]:
            print(" ", ln.strip())
    if other:
        print("=== other diagnostics ===")
        for ln in other[:40]:
            print(" ", ln.strip())
    if os.path.exists(out) and os.path.getsize(out):
        print("OK: %s (%d bytes)" % (out, os.path.getsize(out)))


if __name__ == "__main__":
    main()
