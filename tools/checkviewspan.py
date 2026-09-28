#!/usr/bin/env python3
"""checkviewspan -- does every declared view fit inside the symbol it lands in?

An `ADDR_` whose orig.h comment declares an extent (`int32_t[7]`,
`int16 x,y [6]`, `18 records of 0x38`, `{int16 x, int16 y}[4]`) is a view of
that many bytes. Under AM2_STANDALONE it resolves to some placed C symbol. If
the view runs past that symbol's end, it is reading the NEXT symbol -- which
works in the native build only because placement lays every symbol at its
original VA, adjacent the way the original linker laid them. Under a linker
that places symbols wherever (VC6 LINK.EXE, `make vc6-link ARGS=--run`) the
tail reads foreign bytes.

Nothing else sees this. It compiles, placement is byte-identical, every
pointer table dereference-verifies, and the native build plays -- the bytes
past the end ARE the right ones there. Both cases found so far were one table
transcribed as two symbols:

  ADDR_HUD_SARGE_OFFSETS  six (x,y) cells, 24 bytes; the struct held 16 and
                          the last 8 were a separate `am2_build_menu_end_data`.
                          Under VC6, a loaded game's sarge panel drew two of
                          its six weapons.
  ADDR_FIELD_530_FRAMES   seven frame ids; am2_roach held two and the other
                          five were a separate `am2_roach_score_ints`.

It is a REPORT on comment-declared extents only: a view with no extent in
its comment is not checked, and a comment's extent is prose, parsed by
pattern. It runs against build/armymen2-dev (as checkplacement does) and
passes with a note when that is not built.
"""
import bisect
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ORIG = os.path.join(ROOT, "src", "inject", "orig.h")
BIN = os.path.join(ROOT, "build", "armymen2-dev")

SIZES = {"int8": 1, "uint8": 1, "char": 1, "byte": 1, "bytes": 1,
         "int16": 2, "uint16": 2, "short": 2,
         "int32": 4, "uint32": 4, "int": 4, "dword": 4, "dwords": 4,
         "float": 4, "double": 8}
SCALAR = r'\b(u?int(?:8|16|32)(?:_t)?|char|float|double|dwords?|bytes?)\b'


def _size(word):
    return SIZES.get(re.sub(r'_t$', '', word), 4)


def extent(comment):
    """Bytes a comment declares, or None."""
    r = re.search(r'(\d+)\s+records?\s+of\s+(0x[0-9A-Fa-f]+|\d+)', comment)
    if r:
        return int(r.group(1)) * int(r.group(2), 0)
    r = re.search(r'\{([^}]*)\}\s*\[(\d+)\]', comment)        # {int16 x, ...}[4]
    if r:
        each = sum(_size(w) for w in re.findall(SCALAR, r.group(1)))
        return each * int(r.group(2)) if each else None
    r = re.search(SCALAR + r'[^\[\]\n]{0,20}((?:\[\d+\])+)', comment)
    if r:
        n = 1
        for d in re.findall(r'\[(\d+)\]', r.group(2)):
            n *= int(d)
        e = _size(r.group(1)) * n
        if re.search(r'x,\s*y\s*\[', comment):                 # int16 x,y [6]
            e *= 2
        return e
    return None


def main():
    if not os.path.exists(BIN):
        print("checkviewspan: build/armymen2-dev not built; nothing to check.")
        return 0
    syms = []
    out = subprocess.run(["nm", "-S", "--defined-only", BIN],
                         capture_output=True, text=True).stdout
    for line in out.splitlines():
        p = line.split()
        if len(p) == 4 and p[2] in "dDbBrR":
            syms.append((int(p[0], 16), int(p[1], 16), p[3]))
    syms.sort()
    starts = [s[0] for s in syms]

    text = open(ORIG).read()
    checked = bad = 0
    for m in re.finditer(r'#define\s+(ADDR_\w+)\s+0x([0-9A-Fa-f]{8})u?\s*'
                         r'/\*(.*?)\*/', text, re.S):
        name, va, comment = m.group(1), int(m.group(2), 16), m.group(3)
        e = extent(comment)
        if not e:
            continue
        k = bisect.bisect_right(starts, va) - 1
        if k < 0:
            continue
        lo, size, sym = syms[k]
        if not lo <= va < lo + size:
            continue                    # not inside a placed symbol
        checked += 1
        if va + e > lo + size:
            bad += 1
            print("checkviewspan: %s (0x%08X, %d bytes) runs %d bytes past "
                  "the end of %s -- it reads the NEXT symbol, which is only "
                  "adjacent at native placement"
                  % (name, va, e, va + e - (lo + size), sym))
    if bad:
        print("  make the view's whole extent ONE object (a struct member or "
              "a larger array), not two symbols that happen to be adjacent.")
        return 1
    print("checkviewspan: %d declared views each fit inside their symbol"
          % checked)
    return 0


if __name__ == "__main__":
    sys.exit(main())
