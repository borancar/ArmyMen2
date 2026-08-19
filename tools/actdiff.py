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


def norm(w):
    """Heap pointers shift as soon as anything earlier allocates differently,
    so one real divergence makes every later string look wrong. Masking them
    shows the logic differences on their own; AM2_ACTDIFF_RAW=1 keeps them."""
    if os.environ.get("AM2_ACTDIFF_RAW"):
        return w
    v = int(w, 16)
    return "HEAPPTR__" if 0x01000000 <= v < 0x10000000 else w


def load(path):
    """[(file, line, rc, words)] in order."""
    out, cur = [], "?"
    for raw in open(path):
        f = raw.split()
        if raw.startswith("PARSEALL "):
            cur = raw.split(None, 1)[1].strip()
        elif raw.startswith("ACT "):
            out.append((cur, int(f[1]), int(f[2]), [norm(x) for x in f[3:]]))
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
