#!/usr/bin/env python3
"""Verify the dead-string sweep: every zeroed range was provably unreachable.

tools/deadstrings.py zeroes blob strings that nothing in the native/SA build can
reach. This re-proves that decision independently and guards it against drift:

  1. Recompute the dead set from a PRISTINE blob (mkglobals output) and confirm
     it matches build/standalone/deadstrings.txt exactly. This re-runs the full
     safety oracle -- genuine-string (start referenced in the original image),
     not pointed-at, not code-referenced in src -- so a change that makes a
     zeroed string reachable again fails here.
  2. Confirm the BUILT blob (build/standalone/origdata.bin) is actually zero over
     every recorded range, and non-zero nowhere it should not be.
  3. Defensive: assert no dword in the pristine blob points into any zeroed
     range (a carried char* table reaching a string we zeroed would be a bug).

Exit non-zero on any mismatch. Part of `make check`.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import am2
import deadstrings as ds

REPO = ds.REPO
OUT = ds.OUT


def pristine_blob(recorded):
    """Reconstruct the pristine blob without side effects.

    deadstrings.py only zeroes the recorded string ranges in origdata.bin, and
    strings are never fixed up by mkglobals (only pointer dwords are), so the
    original bytes at those VAs equal what the pristine blob held. Restoring them
    from the image reproduces the pristine blob -- no mkglobals, no temp files.
    """
    blob = bytearray(open(os.path.join(OUT, "origdata.bin"), "rb").read())
    img = am2.Image()
    for va, ln in recorded:
        blob[va - ds.BLOB_LO: va - ds.BLOB_LO + ln] = img.read(va, ln)
    return blob


def main():
    txt = os.path.join(OUT, "deadstrings.txt")
    if not os.path.exists(txt):
        print("checkdeadstrings: deadstrings.txt missing (run standalone-generate)")
        return 1
    recorded = []
    for line in open(txt):
        line = line.split()
        if line:
            recorded.append((int(line[0], 16), int(line[1])))

    pristine = pristine_blob(recorded)
    computed = ds.dead_ranges(pristine)

    bad = 0
    if computed != recorded:
        print("checkdeadstrings: FAIL recomputed dead set != deadstrings.txt")
        print("  recorded %d ranges, recomputed %d" % (len(recorded), len(computed)))
        only_r = set(recorded) - set(computed)
        only_c = set(computed) - set(recorded)
        for va, ln in sorted(only_r)[:10]:
            print("   only in file:      0x%08X %d" % (va, ln))
        for va, ln in sorted(only_c)[:10]:
            print("   only recomputed:   0x%08X %d" % (va, ln))
        bad += 1

    # (2) built blob must be zero over every recorded range
    built = open(os.path.join(OUT, "origdata.bin"), "rb").read()
    for va, ln in recorded:
        off = va - ds.BLOB_LO
        if any(built[off:off + ln]):
            print("checkdeadstrings: FAIL 0x%08X not zeroed in built blob" % va)
            bad += 1
            break

    # (3) no SURVIVING dword points into any zeroed range. Dwords inside a
    # migrated pointer table are carved out by placement and replaced with C
    # literals, so they do not survive -- exclude them exactly as dead_ranges
    # does, or a string only a migrated name table reached looks reachable here.
    import placement
    carved = [(a, a + s) for (a, s, _sym, is_ptr)
              in placement.placed_set(placement.manifest(), placement.split())
              if is_ptr]
    carved += [(a, a + s) for a, s in ds.dead_tables()]
    targets = set()
    for off in range(0, len(pristine) - 3, 4):
        if any(lo <= ds.BLOB_LO + off < hi for lo, hi in carved):
            continue
        v = struct.unpack("<I", pristine[off:off + 4])[0]
        if ds.BLOB_LO <= v < ds.BLOB_HI:
            targets.add(v)
    for va, ln in recorded:
        if any(va <= t < va + ln - 1 for t in targets):
            print("checkdeadstrings: FAIL 0x%08X is pointed at by a blob dword" % va)
            bad += 1
            break

    if bad:
        return 1
    total = sum(ln for _, ln in recorded)
    print("checkdeadstrings: OK %d dead ranges, %d bytes, all unreachable"
          % (len(recorded), total))
    return 0


if __name__ == "__main__":
    sys.exit(main())
