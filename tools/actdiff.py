#!/usr/bin/env python3
"""Compare two AM2_DUMP_ACTIONS logs and say which action diverged.

A record on its own is 18 hex words. What makes a difference actionable is the
script line it came from and the keyword on that line, so this walks the
PARSEALL markers to know which file each record belongs to and reads the line
out of the file itself.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import am2


def norm(w, seen):
    """Renumber heap pointers in first-seen order.

    Two things make the raw values useless for comparison. One real divergence
    early on shifts every later allocation, so a single bug makes thousands of
    strings look wrong. And the whole heap moves when the injected DLL changes
    size at all -- splitting script.cpp in two displaced every pointer by
    0x70000 while the parse was byte-identical.

    What IS meaningful is which records share a pointer, and renumbering keeps
    that: two records that pointed at the same string still do. AM2_ACTDIFF_RAW
    keeps the addresses, which is only worth doing when comparing two runs of
    the same binary.

    The numbering is per FILE, and that granularity is the whole of its value.
    A single global sequence is what the first version used, and it cascades:
    the sweep frees each file's strings before the next, so the heap hands back
    a different set of addresses when the DLL moves, one first-seen pointer
    lands in a different order, and every index after it shifts by one. That
    reported 32 files diverging on P1922 against P1921 while the parse was
    identical. Allocation order is deterministic within a file and nothing
    across files is worth comparing, so the sequence restarts at each marker.
    """
    if os.environ.get("AM2_ACTDIFF_RAW"):
        return w
    v = int(w, 16)
    if not (0x01000000 <= v < 0x10000000):
        return w
    return "P%d" % seen.setdefault(v, len(seen))


def load(path):
    """[(file, line, rc, words)] in order."""
    out, cur, seen = [], "?", {}
    for raw in open(path):
        f = raw.split()
        if raw.startswith("PARSEALL "):
            cur = raw.split(None, 1)[1].strip()
            seen = {}
        elif raw.startswith("ACT "):
            out.append((cur, int(f[1]), int(f[2]),
                        [norm(x, seen) for x in f[3:]]))
    return out


def source_line(winpath, n):
    rel = winpath.replace("C:\\GOG Games\\Army Men II\\", "").replace("\\", os.sep)
    try:
        with open(os.path.join(am2.GAME_DIR, rel), "rb") as fh:
            for k, line in enumerate(fh):
                if k == n:
                    return line.decode("latin-1").strip()[:70]
    except OSError:
        pass
    return "?"


def main():
    a, b = load(sys.argv[1]), load(sys.argv[2])
    print("reference %d records, ours %d" % (len(a), len(b)))

    # Group by file so one bad action does not misalign everything after it.
    from collections import defaultdict, Counter
    fa, fb = defaultdict(list), defaultdict(list)
    for f, l, rc, w in a:
        fa[f].append((l, rc, w))
    for f, l, rc, w in b:
        fb[f].append((l, rc, w))

    first = Counter()
    shown = 0
    for f in sorted(fa):
        ra, rb = fa[f], fb.get(f, [])
        for i in range(max(len(ra), len(rb))):
            x = ra[i] if i < len(ra) else None
            y = rb[i] if i < len(rb) else None
            if x == y:
                continue
            line = x[0] if x else (y[0] if y else -1)
            src = source_line(f, line)
            first[src.split()[0].lower() if src.split() else "?"] += 1
            if shown < int(os.environ.get("SHOW", "8")):
                print("\n%s line %d: %s" % (os.path.basename(
                    f.replace("\\", "/")), line, src))
                if x:
                    print("   want rc %d %s" % (x[1], " ".join(x[2])))
                if y:
                    print("   got  rc %d %s" % (y[1], " ".join(y[2])))
                shown += 1
            break               # one report per file: the first divergence
    print("\nfirst-divergence keyword histogram:")
    for k, n in first.most_common(25):
        print("   %-22s %d" % (k, n))


if __name__ == "__main__":
    main()
