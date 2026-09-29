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

# GCC-only TUs (POSIX).
SKIP = {"crt.cpp"}   # trig.cpp (_MSC_VER asm) and dplay.cpp (win32.h DirectPlay compat) compile under VC6 now

IMPORT_LIBS = ["KERNEL32.LIB", "USER32.LIB", "GDI32.LIB", "ADVAPI32.LIB",
               "OLE32.LIB", "DDRAW.LIB", "DSOUND.LIB", "DINPUT.LIB",
               "WINMM.LIB", "WS2_32.LIB", "DXGUID.LIB", "LIBCMT.LIB"]
# The MULTITHREADED static CRT (and /MT below): the game runs its own threads
# (the comm packet thread, winmm timer callbacks), and single-threaded LIBC.LIB
# corrupts shared stdio/heap state across them.


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
        # NO carried blob at all -- the runnable image must not need one.  In
        # its place, a NO-ACCESS guard section laid right after .text (bss
        # class, so LINK puts it before .rdata/.data) and big enough to cover
        # the original data range 0x46F000..0x666000 wherever .text ends.  Any
        # access through a raw original address -- an un-migrated ADDR, or a
        # pointer still baked into transcribed data -- now FAULTS at a clear
        # address instead of silently reading some other section's bytes.
        mkcoff.emit(path, [
            {"name": ".guard", "chars": mkcoff.CNT_UNINITIALIZED_DATA,
             "bsssize": 0x00270000, "syms": [("_am2_guard_base", 0)]},
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
    extra += ("/MT",)   # match LIBCMT.LIB
    # VC6_CFLAGS adds compiler switches for a diagnostic build -- e.g. "/Oy-" keeps
    # frame pointers so winedbg can unwind a hang (/O2 omits them).
    extra += tuple(os.environ.get("VC6_CFLAGS", "").split())
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


# SMACKW32.DLL has no import library in the SDK, and movie.cpp calls it through
# dllimport (as the original does, so the relink matches).  The DLL exports the
# DECORATED names (`_SmackOpen@12`), so the import library LINK -lib makes from
# a .def -- which records "strip the underscore" (name type 2) for a stdcall
# name -- would ask the loader for `SmackOpen@12`.  Each short import object's
# name type is patched to 1, import by the exact symbol name.
SMACK_IMPORTS = ["SmackVolumePan@16", "SmackToBuffer@28", "SmackDoFrame@4",
                 "SmackNextFrame@4", "SmackWait@4", "SmackClose@4",
                 "SmackSoundUseDirectSound@4", "SmackOpen@12",
                 "SmackDDSurfaceType@4"]


def make_smack_lib():
    import struct
    with open(os.path.join(OUTDIR, "smackw32.def"), "w") as f:
        f.write("LIBRARY smackw32.dll\nEXPORTS\n")
        for n in SMACK_IMPORTS:
            f.write("  %s\n" % n)
    with open(os.path.join(OUTDIR, "smack.rsp"), "w") as f:
        f.write("/NOLOGO\n/DEF:smackw32.def\n/MACHINE:IX86\n/OUT:SMACKW32.LIB\n")
    env = dict(os.environ, LIB=LIBDIR, VC6_ROOT=VC6_ROOT)
    subprocess.run([HOST, LINK, "-lib", "@smack.rsp"], env=env, cwd=OUTDIR,
                   capture_output=True, text=True)
    path = os.path.join(OUTDIR, "SMACKW32.LIB")
    d = bytearray(open(path, "rb").read())
    i = 0
    while True:
        i = d.find(b"\x00\x00\xff\xff", i)
        if i < 0:
            break
        ver, mach = struct.unpack_from("<HH", d, i + 4)
        if ver == 0 and mach == 0x14C:
            typ = struct.unpack_from("<H", d, i + 18)[0]
            struct.pack_into("<H", d, i + 18, (typ & ~0x1C) | (1 << 2))
        i += 4
    open(path, "wb").write(bytes(d))
    return "SMACKW32.LIB"


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
        f.write("\n".join(IMPORT_LIBS + [make_smack_lib()]) + "\n")
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
        if args.run:
            guard_noaccess(out)
        print("OK: %s (%d bytes)" % (out, os.path.getsize(out)))


def guard_noaccess(exe):
    """LINK forces MEM_READ|MEM_WRITE onto every data section, so strip them
    from .guard's header after the link: a section with no R/W/X is mapped
    PAGE_NOACCESS, turning every raw original-address access into a fault."""
    import struct
    d = bytearray(open(exe, "rb").read())
    pe = struct.unpack_from("<I", d, 0x3C)[0]
    nsec = struct.unpack_from("<H", d, pe + 6)[0]
    opt = struct.unpack_from("<H", d, pe + 20)[0]
    sec = pe + 24 + opt
    for i in range(nsec):
        h = sec + 40 * i
        if d[h:h + 8].rstrip(b"\0") == b".guard":
            struct.pack_into("<I", d, h + 36, mkcoff.CNT_UNINITIALIZED_DATA)
            open(exe, "wb").write(d)
            print("guard: .guard made no-access")
            return
    print("guard: WARNING no .guard section found")


if __name__ == "__main__":
    main()
