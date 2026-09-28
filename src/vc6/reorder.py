#!/usr/bin/env python3
"""src/vc6/reorder.py SRC [--apply] -- order SRC's installed function definitions by
their original address.

Everything that is not an installed function (includes, macros, typedefs,
data, static helpers, #if blocks) keeps its relative order and comes FIRST;
then every installed function at preprocessor depth 0, with the comment block
directly above it, sorted by the VA the manifest gives it. VC6 infers
"cannot throw" only for callees defined EARLIER in the TU, and the original's
definition order is its address order, so this is what makes our unwind
frames and states come out as the original's.

Dry run prints what would move."""
import os, re, sys
sys.path.insert(0, "src/vc6")
import bytematch as BM

src = sys.argv[1]
apply = "--apply" in sys.argv
lines = open(src).read().split("\n")

man = {}
for f, name, va in BM.manifest():
    if os.path.normpath(f) == os.path.normpath(src):
        man.setdefault(name, va)


def fname(header):
    h = " ".join(header.split())
    m = re.search(r'(\w+)::~(\w+)\s*\(', h)
    if m and m.group(1) == m.group(2):
        return "??1" + m.group(1)
    m = re.search(r'(\w+)::(\w+)\s*\(', h)
    if m:
        if m.group(1) == m.group(2):
            return "??0" + m.group(1)
        return m.group(2)
    m = re.search(r'(\w+)\s*\(', h)
    return m.group(1) if m else None


IF = re.compile(r'^\s*#\s*if')
ENDIF = re.compile(r'^\s*#\s*endif')
chunks = []          # (kind, name, va, [lines])
i, n, depth = 0, len(lines), 0
other = []
while i < n:
    l = lines[i]
    starts_fn = (depth == 0 and l and not l[0].isspace() and l[0] not in "#/}*{"
                 and "(" in l and not l.rstrip().endswith(";") and not l.startswith("typedef")
                 and not l.startswith("static const") and "=" not in l.split("(")[0])
    if starts_fn:
        # the header runs to a `{` line at column 0
        j = i
        while j < n and lines[j] != "{" and not lines[j].rstrip().endswith(";"):
            j += 1
        if j < n and lines[j] == "{":
            k = j
            while lines[k] != "}":
                k += 1
            header = "\n".join(lines[i:j])
            name = fname(header)
            if name in man:
                # take the comment block directly above
                c = len(other)
                while c > 0 and (other[c - 1].startswith(("/*", " *", "//")) or other[c - 1].startswith(" */")):
                    c -= 1
                if c < len(other) and not other[c].startswith(("/*", "//")):
                    c = len(other)      # not a whole comment block; leave it
                cm = other[c:]
                del other[c:]
                chunks.append(("other", None, None, other)); other = []
                chunks.append(("fn", name, man[name], cm + lines[i:k + 1]))
                i = k + 1
                continue
    if IF.match(l): depth += 1
    elif ENDIF.match(l): depth -= 1
    other.append(l)
    i += 1
chunks.append(("other", None, None, other))

fns = [c for c in chunks if c[0] == "fn"]
names = [c[1] for c in fns]
dups = {x for x in names if names.count(x) > 1}
assert not dups, dups
order = sorted(fns, key=lambda c: c[2])
moved = sum(1 for a, b in zip(fns, order) if a is not b)
print("%d installed functions, %d change position" % (len(fns), moved))
if not apply:
    for c in order[:10]:
        print("  0x%08x %s" % (c[2], c[1]))
    sys.exit(0)

out = []
for c in chunks:
    if c[0] == "other":
        out += c[3]
# strip trailing blank lines, then the functions in address order
while out and out[-1] == "":
    out.pop()
for c in order:
    out += [""] + c[3]
out.append("")
open(src, "w").write("\n".join(out))
