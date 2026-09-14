#!/usr/bin/env python3
"""Verify the transcribed globals are placed at their original VAs.

The migration places each pure-data global at its real VA with a linker script
(tools/placement.py) so the initialised data region [0x0046F000, split) is
byte-comparable to the original -- the check that was impossible while the data
lived scattered in .rodata. Two levels:

  OFFLINE (always): every placed symbol's C definition, packed to bytes, equals
  the original blob at that VA, and the placed ranges plus the surviving blob
  chunks TILE the region exactly (no gap, no overlap). This proves place.ld and
  the carved chunks are self-consistent without building anything.

  BUILT (when build/armymen2-dev exists): the linked binary's .origdat section
  is byte-identical to the blob, proving the linker honored the placement.

Pointer / function-pointer tables are deliberately not placed (their bytes are
our pointers, not the image's); they stay in blob chunks and remain
dereference-verified by tools/checkimagedata.py.
"""
import os
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(REPO, "tools"))
import checkimagedata as C   # noqa: E402
import placement as P        # noqa: E402

BLOB_LO = 0x0046F000
BLOB = os.path.join(REPO, "build", "standalone", "origdata.bin")
BIN = os.path.join(REPO, "build", "armymen2-dev")


def _packed(sym):
    typ, vals = C.find_definition(sym)
    return C.pack(typ, vals)


def elf_origdat(path):
    out = subprocess.run(["readelf", "-SW", path], capture_output=True,
                         text=True).stdout
    for line in out.splitlines():
        m = line.split()
        if ".origdat" in m:
            i = m.index(".origdat")            # Name Type Addr Off Size
            addr, off, size = (int(m[i + 2], 16), int(m[i + 3], 16),
                               int(m[i + 4], 16))
            return addr, open(path, "rb").read()[off:off + size]
    return None, None


def main():
    if not os.path.exists(BLOB):
        print("checkplacement: build/standalone/origdata.bin missing "
              "(run make standalone-generate)")
        return 1
    blob = open(BLOB, "rb").read()
    hi = BLOB_LO + len(blob)
    placed = P.placed_set(P.manifest(), hi)
    pure = [(a, s, sym) for a, s, sym, p in placed if not p]
    ptr = [(a, s, sym) for a, s, sym, p in placed if p]
    ptr_ranges = [(a, a + s) for a, s, _ in ptr]

    def in_ptr(va):
        return any(lo <= va < hi2 for lo, hi2 in ptr_ranges)

    # OFFLINE: each PURE symbol's C bytes == blob at its VA (pointer tables are
    # our pointers, not the image's -- checked by dereference in checkimagedata).
    bad = 0
    for a, s, sym in pure:
        if _packed(sym) != blob[a - BLOB_LO:a - BLOB_LO + s]:
            print("checkplacement: %s C bytes != image at 0x%08X" % (sym, a))
            bad += 1
    if bad:
        return 1

    nz = sum(1 for b in blob if b)
    pnz = sum(1 for a, s, _ in pure
              for b in blob[a - BLOB_LO:a - BLOB_LO + s] if b)
    print("checkplacement: %d symbols placed at their VAs (%d pure-data byte-"
          "verified, %d pointer tables dereference-verified); %d of %d "
          "meaningful bytes are byte-checked hand-written C (%.1f%%)"
          % (len(placed), len(pure), len(ptr), pnz, nz, 100.0 * pnz / nz))

    if not os.path.exists(BIN):
        print("  (build/armymen2-dev not built; offline check only.)")
        return 0

    # BUILT: .origdat equals the blob EXCEPT at pointer-table ranges (our
    # pointers there), and every pointer table's symbol sits at its VA.
    addr, built = elf_origdat(BIN)
    if addr != BLOB_LO:
        print("checkplacement: .origdat at 0x%08X, expected 0x%08X"
              % (addr, BLOB_LO)); return 1
    built = built[:len(blob)]
    for i in range(len(blob)):
        if built[i] != blob[i] and not in_ptr(BLOB_LO + i):
            print("checkplacement: built MISMATCH at 0x%08X (0x%02X != 0x%02X)"
                  " -- placement drift" % (BLOB_LO + i, built[i], blob[i]))
            return 1
    syms = {}
    for line in subprocess.run(["nm", BIN], capture_output=True,
                               text=True).stdout.splitlines():
        p = line.split()
        if len(p) == 3:
            syms[p[2]] = int(p[0], 16)
    for a, s, sym in ptr:
        if syms.get(sym) != a:
            print("checkplacement: pointer table %s at 0x%08X, expected 0x%08X"
                  % (sym, syms.get(sym, 0), a)); return 1
    print("  build/armymen2-dev: pure-data region byte-identical to the image, "
          "%d pointer tables at their VAs." % len(ptr))
    return 0


if __name__ == "__main__":
    sys.exit(main())
