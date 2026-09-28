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


# src/standalone data/glue the RUNNABLE build needs (the placed const tables and
# blob-glue the game TUs reference by symbol).  NOT runtime.cpp (pulls platform.h
# + the reconstructed CRT, GCC-only) -- vc6run.cpp replaces it.
SA_EXTRA = ["src/standalone/origtail.cpp", "src/standalone/tables.cpp",
            "src/standalone/fixups.cpp", "src/standalone/imports.cpp"]


def tu_list(standalone=False):
    srcs = []
    for pat in ("src/game/*.cpp", "src/game/win32/*.cpp"):
        srcs += sorted(glob.glob(os.path.join(REPO, pat)))
    srcs = [s for s in srcs if os.path.basename(s) not in SKIP]
    if standalone:
        srcs += [os.path.join(REPO, p) for p in SA_EXTRA]
    return srcs


def make_blob(path, carved=False):
    """The carried data as COFF sections LINK places (at whatever VA).

    carved=False (link milestone): the FULL byte-exact rdata+data, so the
    blob's own absolute self-pointers stay internally consistent.

    carved=True (runnable build): the standalone build's CARVED origdata.bin
    (holes where migrated globals were), which the game TUs define as symbols.
    Whatever the blob still reaches by absolute VA -- its own carved-out holes,
    or an un-migrated global -- is now at the WRONG address (the blob is not at
    0x46F000), and that fault is the absolute-address reliance to migrate away.
    """
    if carved:
        binp = os.path.join(REPO, "build", "standalone", "origdata.bin")
        origdat = open(binp, "rb").read()
        bss_size = 0x00666000 - 0x0048E000
        mkcoff.emit(path, [
            {"name": ".origdat", "chars": mkcoff.DATA, "data": origdat,
             "syms": [("_am2_origdat_base", 0)]},
            {"name": ".origbss", "chars": mkcoff.BSS, "bsssize": bss_size,
             "syms": [("_am2_origbss_base", 0)]},
        ])
        return path
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


def compile_all(standalone=False):
    os.makedirs(OUTDIR, exist_ok=True)
    # The runnable build compiles WITH the placed-symbol model (AM2_STANDALONE):
    # globals become C structs/symbols and the DirectX/IAT imports redirect to
    # symbols, so LINK can place sections anywhere.  The link-only milestone
    # compiles WITHOUT it (absolute VAs) -- which is exactly the reliance the
    # runnable build removes.
    extra = ("/DAM2_STANDALONE", "/I" + os.path.join(REPO, "build", "standalone")) if standalone else ()
    objs, fails = [], []
    for s in tu_list(standalone):
        rel = os.path.relpath(s, REPO)
        obj = BM.compile_file(rel, OUTDIR, extra=extra)
        (objs if obj else fails).append(obj or rel)
    glue = "src/vc6/vc6run.cpp" if standalone else "src/vc6/glue.cpp"
    g = BM.compile_file(glue, OUTDIR, extra=extra) if os.path.exists(os.path.join(REPO, glue)) else None
    if g:
        objs.append(g)
    elif os.path.exists(os.path.join(REPO, glue)):
        fails.append(glue)
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
        f.write("/MAP:ArmyMen2.map\n")  # symbol map, to translate a run's crash EIP
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
    ap.add_argument("--run", action="store_true",
                    help="build the RUNNABLE symbol-relative image (AM2_STANDALONE, carved blob)")
    # Ignore stray args: the Makefile's global ARGS (default "-nointro -dbg" for
    # `run`) is passed through to $(ARGS); this is a report tool, not the game.
    args, _ = ap.parse_known_args()
    if not os.path.exists(HOST):
        sys.exit("build/vc6host missing -- run `make vc6`")

    glue_obj = "vc6run.obj" if args.run else "glue.obj"
    if args.relink:
        objs = [os.path.join(OUTDIR, os.path.basename(s)[:-4] + ".obj") for s in tu_list(args.run)]
        objs = [o for o in objs if os.path.exists(o)]
        if os.path.exists(os.path.join(OUTDIR, glue_obj)):
            objs.append(os.path.join(OUTDIR, glue_obj))
        print("reusing %d existing objs" % len(objs))
    else:
        print("compiling %d TUs with VC6 CL%s ..." % (len(tu_list(args.run)), " (AM2_STANDALONE)" if args.run else ""))
        objs, fails = compile_all(standalone=args.run)
        print("  compiled %d, failed %d" % (len(objs), len(fails)))
        for f in fails:
            print("    FAIL", f)

    blob = make_blob(os.path.join(OUTDIR, "origblob.obj"), carved=args.run)
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
