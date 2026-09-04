#!/usr/bin/env python3
"""Generate the data image the standalone build places at the original's VAs.

THE STANDALONE BUILD is an EXE that replaces ArmyMen2.exe in the game folder,
needing the original at BUILD time only.  None of the original's CODE is
required -- measured: of the 1,685 `.text` addresses src/game names, 1,640
are `patch_replace` targets a standalone build has nothing to detour, and the
other 46 are the CRT, rand and log seams that become libc.

WHY THE DATA KEEPS THE ORIGINAL'S ADDRESSES, having first been generated as
1,363 separate C objects.  That version linked and ran and faulted before its
first log line, and the reason is a fact about this image rather than a bug:
its RELOCATIONS ARE STRIPPED, and its .rdata and .data hold 3,582 dwords that
look like pointers into .rdata/.data.  Some are -- 0x004702C0 -> 0x004702D8,
and a run of them at 0x18 intervals.  Some are not: 0x00606060 is the ASCII
"```".  Nothing distinguishes them, so moving the data breaks the real ones
and rewriting by pattern corrupts the strings among them.

Placed back at the addresses they were written for, all 3,582 are correct and
none needs touching.  The section is OURS -- the binary depends on no other
file at run time -- it simply occupies the same range, and our own code is
linked above it at 0x00700000.

WHAT STILL NEEDS FIXING UP is the other direction: 408 dwords that are
FUNCTION pointers, the 33 menu vtables among them.  Those are unambiguous,
because a reconstructed function's original address is known exactly, and
they are rewritten at startup to point at ours.  The original's .text is not
present, so a pointer that is missed faults at once rather than running
something unexpected.

THIS IS A STAGE, NOT THE DESTINATION.  Every table carved out of the blob and
written as typed, named C data is one less thing depending on the layout, and
when the last one goes so does the placement constraint.  That is the same
piecewise method the functions were reconstructed by.

    tools/mkglobals.py            # writes build/standalone/
"""
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import am2
import merges
import checkclaims as cc

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(REPO, "build", "standalone")

GAP_LO  = 0x00401000          # the original's .text, which we do NOT carry
BLOB_LO = 0x0046F000          # the original's .rdata
BLOB_HI = 0x00666000          # the end of its .data
SECTION = ".origdat"

PATCH = re.compile(r"patch_replace\(\s*(ADDR_\w+)\s*,\s*\(const void \*\)\s*&?(\w+)")


def find_static_init(img, done):
    """The MSVC __xc_a..__xc_z table: the C++ static initializers.

    THE ORIGINAL'S CRT RAN THIS AND OURS DOES NOT.  Every one of these is
    reconstructed -- tools/remaining.py reports zero static initializers
    outstanding -- but in a standalone build nothing calls them, so the
    globals they set stay null.  It surfaced as a write to address 0 inside
    SetGamePalette: g_remapIdent is a POINTER global, and InitRemapIdentity
    is what fills it.

    The entries are `jmp` THUNKS rather than the functions themselves, which
    is why searching the data for a reconstructed address does not find the
    table.  Each is followed to its target.
    """
    import capstone
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    best = None
    for s in img.pe.sections:
        if s.Name.decode().rstrip("\0") != ".data":
            continue
        base = s.VirtualAddress + 0x400000
        raw = img.pe.get_data(s.VirtualAddress, s.SizeOfRawData)
        off = 0
        while off + 4 <= len(raw) - 3:
            v = struct.unpack_from("<I", raw, off)[0]
            if not (GAP_LO <= v < 0x0046EB82):
                off += 4
                continue
            start = off
            while off + 4 <= len(raw) - 3:
                v = struct.unpack_from("<I", raw, off)[0]
                if not (GAP_LO <= v < 0x0046EB82):
                    break
                off += 4
            ents = [struct.unpack_from("<I", raw, k)[0]
                    for k in range(start, off, 4)]
            if len(ents) >= 8:
                good = 0
                for v in ents:
                    try:
                        ins = next(md.disasm(img.read(v, 8), v))
                        t = int(ins.op_str, 16) if ins.mnemonic == "jmp" else v
                    except Exception:
                        t = v
                    if t in done:
                        good += 1
                if good == len(ents) and (best is None or len(ents) > len(best[1])):
                    best = (base + start, ents)
    if best is None:
        return None, []
    addr, ents = best
    out = []
    for v in ents:
        try:
            ins = next(md.disasm(img.read(v, 8), v))
            t = int(ins.op_str, 16) if ins.mnemonic == "jmp" else v
        except Exception:
            t = v
        out.append(t)
    return addr, out

def emit_imports(img, out):
    """Bind the original's IAT, because our copy of it was never loaded.

    The .rdata we carry contains the IAT as it sits in the FILE: each slot
    holds an RVA into the hint/name table, not a function address -- the
    loader overwrites them, and no loader ever touches ours.  Several seams
    call through those slots (orig_release_mutex, orig_wait_for_object,
    orig_get_tick_count, the whole Smacker family), so each was jumping to a
    small integer.  That is what 0x00071C6A was: a name-table RVA executed as
    an address.

    Resolving them here fixes the entire class at once rather than one
    `orig_` macro at a time, and it keeps the game's own indirection intact.
    A library that will not load leaves its slots alone: smackw32 is not
    present on every install, and the game tests before calling.
    """
    rows = []
    for entry in getattr(img.pe, "DIRECTORY_ENTRY_IMPORT", []):
        dll = entry.dll.decode()
        for imp in entry.imports:
            if imp.address is None:
                continue
            rows.append((dll, imp.name.decode() if imp.name else None,
                         imp.ordinal, imp.address))
    path = os.path.join(out, "imports.cpp")
    with open(path, "w") as fh:
        fh.write("/* Generated by tools/mkglobals.py -- do not edit.\n"
                 " *\n"
                 " * The original's import table, bound at startup.  See the\n"
                 " * tool's docstring: our copy of .rdata holds the FILE's\n"
                 " * IAT, which no loader has fixed up.\n"
                 " */\n"
                 '#include "%s/src/inject/win32.h"\n' % REPO)
        fh.write('#include <stdint.h>\n\nextern "C" void am2_bind_imports(void);\n\n'
                 "void am2_bind_imports(void)\n{\n    HMODULE m;\n")
        by_dll = {}
        for dll, name, ordinal, addr in rows:
            by_dll.setdefault(dll, []).append((name, ordinal, addr))
        for dll in sorted(by_dll):
            fh.write('\n    m = LoadLibraryA("%s");\n    if (m) {\n' % dll)
            for name, ordinal, addr in by_dll[dll]:
                what = ('"%s"' % name) if name else \
                       "(const char *)(uintptr_t)%du" % ordinal
                fh.write("        *(FARPROC *)(uintptr_t)0x%08Xu = "
                         "GetProcAddress(m, %s);\n" % (addr, what))
            fh.write("    }\n")
        fh.write("}\n")
    return len(rows), len(by_dll)

# Tables MSVC placed in .text rather than .rdata, which a standalone build
# does not carry -- the same shape as the DirectInput rgodf array. Each is
# extracted here and the C is pointed at it, which is the migration this
# design is for: one less thing reading the image.
TEXT_TABLES = (("am2_pickup_kind_index", 0x00433770, 29),)


def emit_tables(img, out):
    path = os.path.join(out, "tables.cpp")
    with open(path, "w") as fh:
        fh.write("/* Generated by tools/mkglobals.py -- do not edit.\n"
                 " *\n"
                 " * Data the original kept inside .text, which the standalone\n"
                 " * build does not carry: the gap there is int3, so reading\n"
                 " * one gave 0xCC and every comparison against it failed.\n"
                 " */\n#include <stdint.h>\n\n")
        for name, addr, n in TEXT_TABLES:
            data = img.read(addr, n)
            fh.write('extern "C" const uint8_t %s[%d];\n' % (name, n))
            fh.write("const uint8_t %s[%d] = {" % (name, n))
            for i, b in enumerate(data):
                fh.write("\n   " if i % 12 == 0 else " ")
                fh.write("0x%02X," % b)
            fh.write("\n};   /* was 0x%08X */\n\n" % addr)
    return len(TEXT_TABLES)

def main():
    os.makedirs(OUT, exist_ok=True)
    img = am2.Image()

    blob = bytearray(BLOB_HI - BLOB_LO)
    for s in img.pe.sections:
        name = s.Name.decode().rstrip("\0")
        if name not in (".rdata", ".data"):
            continue
        va = s.VirtualAddress + 0x400000
        raw = img.pe.get_data(s.VirtualAddress, s.SizeOfRawData)
        off = va - BLOB_LO
        blob[off:off + len(raw)] = raw

    # THE GAP IS FILLED WITH int3, AND THAT IS A DIAGNOSTIC.  The range the
    # original's .text occupied is not carried, but the PE loader commits it
    # as ZEROS -- so a call to an address we failed to redefine lands on
    # `add [eax], al` and SLIDES until eax happens to be null, faulting at an
    # address that has nothing to do with the call.  That is how a missing
    # `free` seam reported itself at 0x004646A9 while nothing in the binary
    # or its data contained that value.  Filled with 0xCC, the fault address
    # IS the intended callee, which orig.h can name.
    with open(os.path.join(OUT, "origgap.S"), "w") as fh:
        fh.write("/* Generated by tools/mkglobals.py -- do not edit.\n"
                 " *\n"
                 " * The original's .text range, filled with int3 so that a\n"
                 " * call into it names the function that is missing.\n"
                 " */\n"
                 "    .section .origgap,\"dw\"\n"
                 "    .globl am2_origgap\n"
                 "am2_origgap:\n"
                 "    .fill %d, 1, 0xCC\n" % (BLOB_LO - GAP_LO))

    binpath = os.path.join(OUT, "origdata.bin")
    open(binpath, "wb").write(bytes(blob))

    with open(os.path.join(OUT, "origdata.S"), "w") as fh:
        fh.write("/* Generated by tools/mkglobals.py -- do not edit.\n"
                 " *\n"
                 " * The original's .rdata and .data, placed by the linker at\n"
                 " * the addresses they were written for.  See the tool's\n"
                 " * docstring for why they cannot simply be moved.\n"
                 " */\n"
                 "    .section %s,\"dw\"\n"
                 "    .globl am2_origdata\n"
                 "am2_origdata:\n"
                 "    .incbin \"%s\"\n" % (SECTION, binpath))

    # The function pointers the original stored in its own data.  A dword
    # equal to a reconstructed function's ORIGINAL address is one; nothing
    # else in this range is unambiguous enough to touch.
    done = set(merges.reconstructed())
    a2n = {}
    for n, a in cc._name_addresses().items():
        a2n.setdefault(int(a, 16), n)

    # The original also stored pointers to its own CRT in its data -- a slot
    # holding `free` at 0x004646A9 is what took the port down after
    # InitApplication. Those addresses are as unambiguous as the
    # reconstructed ones, so they are rewritten too, and the mapping is READ
    # OUT OF src/inject/standalone.h rather than repeated here: two lists of
    # "which address becomes which function" is how they come to disagree.
    seams = {}
    sa = open(os.path.join(REPO, "src", "inject", "standalone.h")).read()
    orig = open(os.path.join(REPO, "src", "inject", "orig.h")).read()
    addr_of = dict((m.group(1), int(m.group(2), 16)) for m in
                   re.finditer(r"^#define\s+(ADDR_\w+)\s+0x([0-9A-Fa-f]+)u?",
                               orig, re.M))
    for m in re.finditer(r"^#define\s+(ADDR_\w+)\s+AM2_SA\((\w+)\)", sa, re.M):
        a = addr_of.get(m.group(1))
        if a is not None and GAP_LO <= a < BLOB_LO:
            seams.setdefault(a, m.group(2))

    fixups = []
    for off in range(0, len(blob) - 3, 4):
        v = struct.unpack_from("<I", blob, off)[0]
        if v in done and v in a2n:
            fixups.append((BLOB_LO + off, a2n[v]))
        elif v in seams:
            fixups.append((BLOB_LO + off, seams[v]))

    import glob
    headers = ([os.path.join(REPO, "src/game/win32/widget.h")]
               + sorted(glob.glob(os.path.join(REPO, "src/game/*.h")))
               + sorted(glob.glob(os.path.join(REPO, "src/game/win32/*.h"))))
    with open(os.path.join(OUT, "fixups.cpp"), "w") as fh:
        fh.write("/* Generated by tools/mkglobals.py -- do not edit.\n"
                 " *\n"
                 " * The original stored pointers to its own functions in its\n"
                 " * own data -- the 33 menu vtables among them.  Its code is\n"
                 " * not in this binary, so each is rewritten to ours before\n"
                 " * WinMain runs.\n"
                 " */\n"
                 '#include "%s/src/inject/win32.h"\n' % REPO)
        fh.write('#include "%s/src/inject/orig.h"\n' % REPO)
        for h in headers:
            fh.write('#include "%s"\n' % h)
        fh.write("\n#include <stdint.h>\n\n"
                 'extern "C" void am2_apply_fixups(void);\n\n'
                 "void am2_apply_fixups(void)\n{\n")
        for addr, fn in fixups:
            fh.write("    *(const void **)(uintptr_t)0x%08Xu = "
                     "(const void *)%s;\n" % (addr, fn))
        fh.write("}\n")

    # The C++ static initializers.  The patch list is the authoritative name
    # for an address -- it is what is actually installed -- so it wins over
    # the declaration comments here.
    patch_names = {}
    for path in (glob.glob(os.path.join(REPO, "src/game/*.cpp"))
                 + glob.glob(os.path.join(REPO, "src/game/win32/*.cpp"))):
        for macro, fn in PATCH.findall(open(path).read()):
            a = addr_of.get(macro)
            if a is not None:
                patch_names[a] = fn
    si_addr, si_entries = find_static_init(img, done)
    si = []
    for t in si_entries:
        nm = patch_names.get(t) or a2n.get(t)
        if nm:
            si.append(nm)
    with open(os.path.join(OUT, "staticinit.cpp"), "w") as fh:
        fh.write("/* Generated by tools/mkglobals.py -- do not edit.\n"
                 " *\n"
                 " * The original's C++ static initializers, in the order its\n"
                 " * __xc_a..__xc_z table lists them.  The MSVC CRT ran these\n"
                 " * before WinMain; ours does not, so the standalone build\n"
                 " * calls them itself -- without which the globals they fill\n"
                 " * stay null, and SetGamePalette writes through one of them.\n"
                 " */\n"
                 '#include "%s/src/inject/win32.h"\n' % REPO)
        for h in headers:
            fh.write('#include "%s"\n' % h)
        fh.write('#include "%s/src/game/crt.h"\n' % REPO)
        fh.write('\nextern "C" void am2_run_static_init(void);\n\n'
                 "void am2_run_static_init(void)\n{\n")
        for nm in si:
            fh.write("    %s();\n" % nm)
        fh.write("}\n")

    nimp, ndll = emit_imports(img, OUT)

    ntab = emit_tables(img, OUT)

    print("mkglobals:")
    print("  %s  0x%06X..0x%06X  %d bytes"
          % (SECTION, BLOB_LO, BLOB_HI, len(blob)))
    print("  function-pointer fixups : %d" % len(fixups))
    print("  static initializers     : %d of %d resolved"
          % (len(si), len(si_entries)))
    print("  imports bound           : %d from %d dll(s)" % (nimp, ndll))
    print("  .text tables extracted  : %d" % ntab)
    print("  written to              : build/standalone/")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
