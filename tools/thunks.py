#!/usr/bin/env python3
"""Find the incremental-linker thunks that hide function boundaries.

`docs/functions.tsv` is built from a symbol-free image and runs neighbours
together wherever it cannot see a boundary.  CLAUDE.md records ~128 merged
sub-CRT entries covering 90 KB without saying WHY they merge.  Here is one
reason, and it is structural rather than a limitation of any one scan.

This executable was INCREMENTALLY LINKED.  MSVC's incremental linker emits a
five-byte `jmp rel32` thunk for a function and points every call site at the
thunk, so the real function can move without any call site being patched.  The
consequence for a cross-reference scan is that the real function has exactly
ONE referrer -- its own thunk -- and nothing references the thunk in a form the
scan recognises.  Neither boundary is visible, so `tools/merges.py` correctly
reports no split and the entry swallows both.

WHY THIS IS NOT COSMETIC.  The project's stop condition is "every game function
below the CRT line is patched -- measure it, do not estimate it", and that
measurement counts ENTRIES.  Patching one function in an entry that holds
twelve marks the entry covered, so the outstanding count can reach zero with
eleven functions still original.  Merged entries do not merely flatter the
number; they can let the goal be declared met while the work is unfinished.

The signature is narrow on purpose: an `E9 rel32` bracketed by 0x90 or 0xCC
padding, jumping FORWARD by less than 0x40 bytes.  Padding on both sides is
what distinguishes a linker thunk from an ordinary tail jump, which sits inside
a function's body with real instructions either side.

Prints one line per thunk with the entry it falls in, and a summary of which
entries are affected.  It changes no count on its own -- wiring these targets
into merges.py as known splits is a separate decision, because it moves a
number every report quotes.
"""
import csv
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import am2

CRT_START = 0x0045C000
TEXT_LO = 0x00401000
PAD = (0x90, 0xCC)
MAX_HOP = 0x40


def thunks(img):
    """Every padded short forward `jmp rel32` below the CRT line."""
    data = img.read(TEXT_LO, CRT_START - TEXT_LO)
    out = []
    for i in range(1, len(data) - 6):
        if data[i] != 0xE9 or data[i - 1] not in PAD or data[i + 5] not in PAD:
            continue
        site = TEXT_LO + i
        target = site + 5 + struct.unpack('<i', data[i + 1:i + 5])[0]
        if 0 < target - site < MAX_HOP:
            out.append((site, target))
    return out


def entries():
    rows = []
    with open(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                           '..', 'docs', 'functions.tsv')) as fh:
        for r in csv.DictReader(fh, delimiter='\t'):
            a = int(r['addr'], 16)
            if a < CRT_START:
                rows.append((a, int(r.get('size', '0') or 0)))
    rows.sort()
    return rows


def entry_of(rows, addr):
    lo, hi = 0, len(rows) - 1
    best = None
    while lo <= hi:
        mid = (lo + hi) // 2
        if rows[mid][0] <= addr:
            best = rows[mid]
            lo = mid + 1
        else:
            hi = mid - 1
    if best and best[0] <= addr < best[0] + best[1]:
        return best
    return None


def main():
    img = am2.Image()
    rows = entries()
    found = thunks(img)
    affected = {}
    for site, target in found:
        e = entry_of(rows, site)
        key = e[0] if e else None
        affected.setdefault(key, []).append((site, target))
        print('  %08x -> %08x   in entry %s' %
              (site, target,
               ('%08x (%d B)' % (e[0], e[1])) if e else '(none)'))
    print()
    print('%d thunk(s) in %d entr(ies) below the CRT line.'
          % (len(found), len(affected)))
    for key in sorted(k for k in affected if k is not None):
        e = entry_of(rows, key)
        print('  entry %08x (%4d B) hides %2d thunk/function pair(s)'
              % (key, e[1], len(affected[key])))
    print()
    print('Each pair is a boundary functions.tsv cannot see, so patching one')
    print('function in such an entry credits the whole entry. See the')
    print('docstring for why that bears on the stop condition.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
