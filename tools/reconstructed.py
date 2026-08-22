#!/usr/bin/env python3
"""How much of the game below the CRT line is reconstructed.

The figure STATUS.md carries, and the reason this is a tool rather than a
line of shell: it was recomputed by an ad-hoc script every session, and every
one of those scripts asked "does a patched address fall inside this
functions.tsv entry", which credits the WHOLE entry to whichever function in
it was patched.

That is the same defect `tools/coverage.py` was fixed for -- reconstructing
`AudioTimerProc` marked `OpenAudioStream`'s COM calls covered a commit before
they were -- and it bites hardest exactly where the work is easiest. Fifteen
dialog destructors, each a two-instruction function, sit inside fifteen
entries that hold the dialog's whole implementation; patching them moved the
naive figure 3.8 points for about 400 bytes of actual code.

So both numbers are printed. The split one is the answer; the naive one is
kept beside it because it is what every earlier session quoted, and a figure
that silently drops six points would look like the work going backwards.

Neither is "how much of the game runs on our code" -- see the note under
STATUS.md's table.
"""
import csv
import io
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import am2
import merges

CRT_START = 0x0045C000
REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def patched_addresses():
    """Every address a patch_replace call installs at, resolved through orig.h."""
    defs = {}
    header = io.open(os.path.join(REPO, "src", "inject", "orig.h"),
                     encoding="utf-8").read()
    for m in re.finditer(r"#define\s+([A-Za-z0-9_]+)\s+0x([0-9A-Fa-f]+)u?\b",
                         header):
        defs.setdefault(m.group(1), int(m.group(2), 16))

    out = set()
    for src in am2.game_sources():
        text = io.open(src, encoding="utf-8", errors="replace").read()
        for m in re.finditer(r"patch_replace\(\s*([A-Za-z0-9_]+)", text):
            if m.group(1) in defs:
                out.add(defs[m.group(1)])
    return out


def entries():
    for row in csv.DictReader(io.open(os.path.join(REPO, "docs",
                                                   "functions.tsv"),
                                      encoding="utf-8"), delimiter="\t"):
        addr, size = int(row["addr"], 16), int(row["size"])
        if addr < CRT_START:
            yield addr, size


def main():
    patched = patched_addresses()
    img = am2.Image()
    real = merges.real_functions(img)

    split = []
    for addr, size in entries():
        if addr in real:
            starts = sorted(set(real[addr][0]))
            for i, start in enumerate(starts):
                end = starts[i + 1] if i + 1 < len(starts) else addr + size
                split.append((start, end - start))
        else:
            split.append((addr, size))

    def covered(fns):
        return sum(sz for a, sz in fns
                   if any(a <= p < a + sz for p in patched))

    naive = list(entries())
    total = sum(sz for _, sz in naive)

    print("patch_replace addresses      %d (%d below the CRT line)"
          % (len(patched), sum(1 for p in patched if p < CRT_START)))
    print("sub-CRT bytes                %s" % f"{total:,}")
    print()
    print("split at referenced starts   %s / %s  %.1f%%   (%d functions)"
          % (f"{covered(split):,}", f"{total:,}",
             100.0 * covered(split) / total, len(split)))
    print("whole listed entries         %s / %s  %.1f%%   (%d entries)"
          % (f"{covered(naive):,}", f"{total:,}",
             100.0 * covered(naive) / total, len(naive)))
    print()
    print("The first is the figure to quote. The second credits every byte of")
    print("a merged entry to whichever function in it was patched, which is")
    print("what earlier sessions reported; see this file's docstring.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
