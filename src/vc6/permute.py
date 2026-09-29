#!/usr/bin/env python3
"""Try every source order of a run of statements and report which one VC6
compiles closest to the original.

    src/vc6/permute.py SRC FUNC SPEC.json

SPEC is {"items": ["exact text of statement 1\\n", ...]}; the items must
appear consecutively in SRC, in that order, and an item may span several
lines. Each order is written to a scratch copy beside SRC (`_perm_<pid>.cxx`:
CL compiles it as C++, while neither the Makefile nor the manifest globs
.cxx, so SRC and the builds stay usable meanwhile) and compared with
onefn.py. Prints the best orders; SRC is never edited.

Why this exists: VC6's scheduler is order-sensitive in ways reading does not
predict. CheckBox, CountButton and KeyRow each match in exactly one order of
their trailing stores, and no amount of staring at the original's store
order found it -- the emitted order is not the source order. N items cost N!
compiles at a few seconds each, so keep N <= 5 (and run it in the
background).

The score is the count of identical instructions plus those differing only
in a relocated slot; the maximum is the function's instruction count.
"""
import itertools
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import onefn


def main():
    src, fn, spec = sys.argv[1:4]
    items = json.load(open(spec))["items"]
    base = open(src).read()
    block = "".join(items)
    if block not in base:
        sys.exit("items not found consecutively in " + src)
    tmp = os.path.join(os.path.dirname(os.path.abspath(src)), "_perm_%d.cxx" % os.getpid())
    res = []
    try:
        for perm in itertools.permutations(range(len(items))):
            open(tmp, "w").write(base.replace(block, "".join(items[i] for i in perm)))
            try:
                pairs = onefn.compare(tmp, fn)
            except RuntimeError:
                continue
            score = sum(1 for x, y in pairs if x == y or onefn.reloc_only(x, y))
            res.append((score, len(pairs), perm))
    finally:
        if os.path.exists(tmp):
            os.remove(tmp)
    res.sort(reverse=True)
    for score, n, perm in res[:5]:
        print("%d/%d  %s%s" % (score, n, perm, "  <- matches" if score == n else ""))


if __name__ == "__main__":
    main()
