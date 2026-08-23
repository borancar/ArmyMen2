#!/usr/bin/env python3
"""An i386 MSVC constructor returns `this` in eax, and a reconstruction
declared `void` returns whatever happened to be there.

This exists because that defect shipped and then killed the game. `RecordCtor`
(0x00453910) is a three-field constructor; its caller at 0x00451473 stores the
result straight into the SELECT PLAYER dialog's 0x0064, and the list built
there is what ListAdd writes through. Declared `void`, our version left the
`value` argument in eax, the dialog's list pointer became 1, and clicking
MULTI-PLAYER took the process down inside ListAdd. Nothing static saw it: the
body was byte-for-byte correct and only the RETURN was missing.

The tell is cheap and exact. MSVC opens such a function with `mov eax, ecx`
(8B C1) purely so the value survives to the `ret`; nothing else in this image
starts that way for another reason. So: for every address in the patch list,
if the original's first two bytes are 8B C1, the reconstruction must not be
declared `void`.

It is a ratchet rather than a report -- a new one fails the build. Tested in
the failing direction by putting `void` back on RecordCtor, which it catches.
"""
import re
import sys
import os

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import am2

ENTRY = b"\x8b\xc1"


def main():
    img = am2.Image()

    macros = {}
    for m in re.finditer(r"#define\s+(ADDR_[A-Z0-9_]+)\s+(0x[0-9A-Fa-f]+)u",
                         open("src/inject/orig.h").read()):
        macros.setdefault(m.group(1), int(m.group(2), 16))

    bad = []
    seen = 0
    for path in am2.game_sources():
        text = open(path).read()
        for m in re.finditer(
                r"patch_replace\(\s*([A-Z0-9_]+)\s*,\s*\(const void \*\)"
                r"(\w+)\s*,", text):
            addr = macros.get(m.group(1))
            if addr is None:
                continue
            if img.read(addr, 2) != ENTRY:
                continue
            seen += 1
            fn = m.group(2)
            defn = re.search(r"^([A-Za-z_][^;{\n]*?)\b" + re.escape(fn)
                             + r"\s*\(", text, re.M)
            if defn and re.match(r"^\s*void\s+", defn.group(1)) \
                    and "*" not in defn.group(1).split(fn)[0]:
                bad.append((addr, m.group(1), fn, path))

    for addr, macro, fn, path in sorted(bad):
        print("  %08X  %-26s %-22s %s" % (addr, macro, fn, path))
        print("            opens `mov eax, ecx` -- it returns `this`, and this"
              " reconstruction returns void")

    print("checkthis: %d patched function(s) open `mov eax, ecx`, %d declared "
          "void" % (seen, len(bad)))
    if bad:
        print("           An MSVC constructor's return is load-bearing at some"
              " call site.")
        print("           Return the object rather than dropping it.")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
