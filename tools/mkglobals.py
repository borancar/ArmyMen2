#!/usr/bin/env python3
"""Generate real C storage for every global the original kept in its image.

THE STANDALONE BUILD IS A DROP-IN REPLACEMENT, so it can hold no hardcoded
VA and cannot need ArmyMen2.exe present.  What makes that cheap is that
src/game reaches the original's data through exactly one spelling --
`ADDR_<NAME>`, cast to a pointer -- so redefining the macro to the ADDRESS OF
A REAL OBJECT leaves all 3,776 use sites compiling unchanged:

    injected    #define ADDR_AIR_GAUGE_X0 0x00473F20u
    standalone  #define ADDR_AIR_GAUGE_X0 ((uintptr_t)&am2_g_air_gauge_x0)

    *(int32_t *)(uintptr_t)ADDR_AIR_GAUGE_X0     <- unchanged either way

SIZES COME FROM THE GAPS.  An object runs from its own address to the next
named one, which is how the original's own data is tiled; where a name has
no successor in its section the object runs to the section's end.  That is
an approximation and it is checkable -- an object too SHORT would be written
past, and CLAUDE.md's standing warning about a field pointer named as a table
base is exactly the case that makes one too short.

VALUES COME FROM THE IMAGE, not from reading.  An address inside a section's
raw bytes keeps them; anything past raw is .bss and is emitted with no
initialiser so it costs nothing in the source or the binary.

ALIGNMENT IS NOT OPTIONAL.  Every object is a byte array, because the code
casts it to whatever it likes, and a byte array has alignment 1 -- so a
`*(uint32_t *)` through it would be misaligned.  Each carries an explicit
alignment.

WHAT THIS CANNOT SEE is code that walks OFF one global into the next.  In the
image they are contiguous; as separate objects they are not.  The original
does this at least once -- CLAUDE.md records a teardown walking 0x005101F0 up
to ADDR_SCRIPT_CONDITIONS because it is "the next global" -- so
tools/checkadjacent.py exists to look for it.

    tools/mkglobals.py            # writes src/game/standalone/
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import am2

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# Generated build artifacts, not reconstruction: they live under
# build/ so tools that scan src/game for modules do not count
# them (checkclaims saw 36 flat modules instead of 34).
OUT = os.path.join(REPO, "build", "standalone")
ALIGN = 16


# Where address-valued macros are defined.  Not only ADDR_: the widget layer
# names the 33 menu vtables, the HUD bitmaps and a handful of strings the same
# way, and those land in .rdata beside everything else.
SOURCES = ("src/inject/orig.h", "src/game/win32/widget.h",
           "src/game/win32/widget.cpp")


def addr_defs():
    out = []
    for rel in SOURCES:
        text = open(os.path.join(REPO, rel)).read()
        out += [(m.group(1), int(m.group(2), 16)) for m in
                re.finditer(r"^#define\s+(\w+)\s+0x([0-9A-Fa-f]{6,8})u?",
                            text, re.M)]
    return out


def sections(img):
    out = {}
    for s in img.pe.sections:
        name = s.Name.decode().rstrip("\0")
        out[name] = (s.VirtualAddress + 0x400000, s.Misc_VirtualSize,
                     s.SizeOfRawData)
    return out


def cname(macro):
    base = macro[len("ADDR_"):] if macro.startswith("ADDR_") else macro
    return "am2_g_" + base.lower()


def main():
    img = am2.Image()
    secs = sections(img)
    data_secs = [(n,) + secs[n] for n in (".rdata", ".data") if n in secs]

    # Every ADDR_ that lands in a data section, grouped by address so the
    # aliases -- two names on one address -- share one object.
    by_addr = {}
    for macro, a in addr_defs():
        for _, va, vs, raw in data_secs:
            if va <= a < va + vs:
                by_addr.setdefault(a, []).append(macro)
                break

    addrs = sorted(by_addr)
    objs = []
    for i, a in enumerate(addrs):
        sec = next(s for s in data_secs if s[1] <= a < s[1] + s[2])
        end = sec[1] + sec[2]
        nxt = addrs[i + 1] if i + 1 < len(addrs) else end
        size = min(nxt, end) - a
        if size <= 0:
            continue
        raw_end = sec[1] + sec[3]
        init = img.read(a, min(size, max(0, raw_end - a))) if a < raw_end else b""
        if not any(init):
            init = b""
        objs.append((a, by_addr[a], size, init))

    # Any dword inside a copied object that is a reconstructed function's
    # ORIGINAL address is a function pointer -- the 33 menu vtables are made
    # of them -- and copying its bytes would leave it pointing at code this
    # binary does not have.  They are rewritten at startup instead of in the
    # initialiser, so globals.cpp stays plain bytes.
    import struct as _s
    import merges
    import checkclaims as _cc
    done = set(merges.reconstructed())
    a2n = {}
    for n, a in _cc._name_addresses().items():
        a2n.setdefault(int(a, 16), n)
    fixups = []
    for a, macros, size, init in objs:
        for off in range(0, len(init) - 3, 4):
            v = _s.unpack_from("<I", init, off)[0]
            if v in done and v in a2n:
                fixups.append((cname(macros[0]), off, a2n[v]))

    os.makedirs(OUT, exist_ok=True)
    with open(os.path.join(OUT, "globals.h"), "w") as fh:
        fh.write("/* Generated by tools/mkglobals.py -- do not edit.\n"
                 " *\n"
                 " * One object per global the original kept in its own\n"
                 " * image, so the standalone build needs no fixed address\n"
                 " * and no copy of ArmyMen2.exe.\n"
                 " */\n"
                 "#ifndef AM2_STANDALONE_GLOBALS_H\n"
                 "#define AM2_STANDALONE_GLOBALS_H\n\n"
                 "#include <stdint.h>\n\n"
                 '#ifdef __cplusplus\nextern "C" {\n#endif\n\n')
        for a, macros, size, init in objs:
            fh.write("extern uint8_t %s[%d];   /* was 0x%08X */\n"
                     % (cname(macros[0]), size, a))
        fh.write('\n#ifdef __cplusplus\n}\n#endif\n\n#endif\n')

    nbytes = ninit = 0
    with open(os.path.join(OUT, "globals.cpp"), "w") as fh:
        fh.write("/* Generated by tools/mkglobals.py -- do not edit. */\n"
                 '#include "globals.h"\n\n')
        for a, macros, size, init in objs:
            nbytes += size
            decl = ("__attribute__((aligned(%d))) uint8_t %s[%d]"
                    % (ALIGN, cname(macros[0]), size))
            if not init:
                fh.write("%s;\n" % decl)
                continue
            ninit += 1
            fh.write("%s = {" % decl)
            for j, b in enumerate(init):
                fh.write("\n   " if j % 12 == 0 else " ")
                fh.write("0x%02X," % b)
            fh.write("\n};\n")

    with open(os.path.join(OUT, "origaddr.h"), "w") as fh:
        fh.write("/* Generated by tools/mkglobals.py -- do not edit.\n"
                 " *\n"
                 " * The standalone spelling of every data ADDR_.  Included\n"
                 " * by src/inject/orig.h under AM2_STANDALONE, so the use\n"
                 " * sites do not change.\n"
                 " */\n"
                 "#ifndef AM2_STANDALONE_ORIGADDR_H\n"
                 "#define AM2_STANDALONE_ORIGADDR_H\n\n"
                 '#include "globals.h"\n\n')
        for a, macros, size, init in objs:
            for m in macros:
                # orig.h has already defined these as literals, so the
                # standalone spelling has to replace them rather than clash.
                fh.write("#undef %s\n#define %-40s ((uintptr_t)&%s)\n"
                         % (m, m, cname(macros[0])))
        fh.write("\n#endif\n")

    repo = REPO
    import glob as _g
    headers = ([os.path.join(repo, "src/game/win32/widget.h")]
               + sorted(_g.glob(os.path.join(repo, "src/game/*.h")))
               + sorted(_g.glob(os.path.join(repo, "src/game/win32/*.h"))))
    with open(os.path.join(OUT, "fixups.cpp"), "w") as fh:
        fh.write("/* Generated by tools/mkglobals.py -- do not edit.\n"
                 " *\n"
                 " * Function pointers the original stored in its own data:\n"
                 " * the menu vtables and the dispatch tables.  Copying the\n"
                 " * bytes would leave them pointing at code this binary does\n"
                 " * not contain, so am2_apply_fixups() writes ours in.\n"
                 " */\n"
                 '#include "%s/src/inject/win32.h"\n' % repo)
        for h in headers:
            fh.write('#include "%s"\n' % h)
        fh.write('#include "globals.h"\n\n'
                 'extern "C" void am2_apply_fixups(void);\n\n'
                 "void am2_apply_fixups(void)\n{\n")
        for obj, off, fn in fixups:
            fh.write("    *(const void **)(%s + %d) = (const void *)%s;\n"
                     % (obj, off, fn))
        fh.write("}\n")

    print("mkglobals:")
    print("  objects              : %d  (%d addresses, %d names)"
          % (len(objs), len(addrs), sum(len(m) for m in by_addr.values())))
    print("  storage              : %d bytes (%.1f MB)" % (nbytes, nbytes / 1e6))
    print("  with initial values  : %d" % ninit)
    print("  pointer fixups       : %d" % len(fixups))
    print("  written to           : build/standalone/")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
