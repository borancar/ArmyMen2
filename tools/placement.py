#!/usr/bin/env python3
"""Place the transcribed read-only globals at their ORIGINAL virtual addresses.

The migration redirects each `ADDR_X` macro to a C symbol (`am2_x`). Compiled
normally those symbols land wherever the linker puts `.rodata`/`.data` -- at an
address that corresponds to NOTHING in the original image. That is wrong for
three reasons this project learned the hard way:

  * a stored pointer into the original data region (there are thousands) still
    finds the stale blob byte at the real VA, not the migrated value;
  * a global whose ADDRESS is a layout boundary breaks when the symbol moves;
  * the region cannot be byte-diffed against the original, so "how much is
    transcribed" is unmeasurable and a wrong transcription is invisible.

The fix (Boran's directive): place each migrated symbol at its real VA with a
linker script, carving the carried blob so it no longer covers those bytes.
`-fdata-sections` gives every global its own input section (`.rodata.am2_x` /
`.data.am2_x`); this tool emits a linker fragment that lays the surviving blob
chunks and those input sections end to end, in address order, under one
writable `.origdat` output section at 0x0046F000 with SUBALIGN(1) -- so the
location counter flows to each symbol's exact VA with no per-symbol address.

WHAT IS PLACED: the pure-data symbols (numeric / float / byte arrays), whose
bytes equal the image's, so `tools/checkplacement.py` byte-diffs the built
region against the original and it must match. WHAT IS NOT: the pointer and
function-pointer tables (char* name arrays, the AM2_WeaponHandler / AM2_Option /
pointer-mode / state-action tables), whose bytes legitimately differ from the
image (our strings, our reconstructed functions) -- they stay scattered
redirects, verified by dereference in checkimagedata, and the blob keeps the
original's (dead, unread) bytes at their VAs. `pointer_modes` also physically
overlaps `build_menu_rects` in the image, so anything overlapping a
non-placed table is left in the blob too.

Run at standalone-generate, after mkglobals.py has written origdata.bin.
"""
import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(REPO, "tools"))
import checkimagedata as C  # noqa: E402

OUT = os.path.join(REPO, "build", "standalone")
BLOB_LO = 0x0046F000
SH = os.path.join(REPO, "src", "inject", "standalone.h")


def manifest():
    """[(va, size, symbol)] for every migrated symbol, deduped by symbol.

    Pure-data only: char*/mixed (pointer tables) are excluded -- their bytes
    are not the image's. Size is the packed byte length, which for numeric,
    float and byte-array data equals the compiled section size, so the linker's
    location counter flows correctly.
    """
    sh = open(SH).read()
    addrs = C.orig_addresses()
    by_sym = {}
    for macro, addr in addrs.items():
        m = re.search(r'#define\s+' + re.escape(macro) +
                      r'\s+\(\(uintptr_t\)(?:\(const void \*\))?\s*&?(\w+)\)', sh)
        if not m:
            continue
        sym = m.group(1)
        dfn = C.find_definition(sym)
        if not dfn:
            continue
        typ, vals = dfn
        if typ in ("char*", "mixed"):
            continue                       # pointer table -- not placed
        size = len(C.pack(typ, vals))
        if not (BLOB_LO <= addr < 0x0048E000):
            continue
        by_sym.setdefault(sym, (addr, size))   # dedupe aliases
    return sorted((a, s, sym) for sym, (a, s) in by_sym.items())


def split():
    return int(open(os.path.join(OUT, "origbss.addr")).read().strip(), 16)


def placed_set(man, blob_hi):
    """Drop any symbol whose range overlaps another migrated symbol.

    `build_menu_rects` overlaps the `pointer_modes` table the linker packed it
    into; such a symbol cannot be placed without colliding, so it stays in the
    blob (its bytes are the image's there anyway).
    """
    man = [m for m in man if BLOB_LO <= m[0] and m[0] + m[1] <= blob_hi]
    keep = []
    for i, (a, s, sym) in enumerate(man):
        clash = any(j != i and a < man[j][0] + man[j][1] and man[j][0] < a + s
                    for j in range(len(man)))
        if clash:
            continue
        keep.append((a, s, sym))
    return keep


def main():
    blob = bytearray(open(os.path.join(OUT, "origdata.bin"), "rb").read())
    hi = BLOB_LO + len(blob)
    placed = placed_set(manifest(), hi)

    # Carve: walk the region, emit a blob chunk for each run between placed
    # symbols, and reference each placed symbol's input section in order.
    pieces = []          # ("chunk", start, length) | ("sym", symbol)
    cur = BLOB_LO
    chunk_files = []
    for a, s, sym in placed:
        if a > cur:
            pieces.append(("chunk", cur, a - cur))
        pieces.append(("sym", sym))
        cur = a + s
    if cur < hi:
        pieces.append(("chunk", cur, hi - cur))

    # Write chunk .bin slices and one .S that puts each in its own section.
    asm = ["/* Generated by tools/placement.py -- do not edit. */",
           "/* The carried blob, split around the globals placed at their VAs */",
           "#ifdef __ELF__"]
    k = 0
    lines_ld = []
    for p in pieces:
        if p[0] == "chunk":
            _, st, ln = p
            name = "origdat.c%03d" % k
            fn = os.path.join(OUT, "chunk_%03d.bin" % k)
            with open(fn, "wb") as fh:
                fh.write(blob[st - BLOB_LO:st - BLOB_LO + ln])
            chunk_files.append(fn)
            asm.append('    .section .%s,"aw"' % name)
            asm.append('    .incbin "%s"' % fn)
            lines_ld.append("    KEEP(*(.%s))" % name)
            k += 1
        else:
            sym = p[1]
            lines_ld.append("    KEEP(*(.rodata.%s)) KEEP(*(.data.%s))"
                            % (sym, sym))
    asm.append("#endif")
    with open(os.path.join(OUT, "origchunks.S"), "w") as fh:
        fh.write("\n".join(asm) + "\n")

    ld = ["/* Generated by tools/placement.py -- do not edit. */",
          "SECTIONS {",
          "  .origdat 0x%08X : SUBALIGN(1) {" % BLOB_LO,
          "\n".join(lines_ld),
          "  }",
          "} INSERT AFTER .bss;"]
    with open(os.path.join(OUT, "place.ld"), "w") as fh:
        fh.write("\n".join(ld) + "\n")

    print("placement: %d symbols placed at their VAs, %d blob chunks, "
          "%d bytes placed" % (len(placed), k, sum(s for _, s, _ in placed)))


if __name__ == "__main__":
    main()
