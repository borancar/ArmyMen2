"""Cross-check each reconstruction's COM calls against the original's.

A reconstructed function should make the same DirectX calls as the code it
replaces. This counts them on both sides -- the original from
docs/comcalls.tsv, ours by matching `IDirectSomething_Method(` in the
function body -- and prints the pairs that disagree.

It is a review aid, not a test, and it will not pass cleanly. Two things make
it disagree without anything being wrong, and both are worth knowing before
reading its output. A third used to, and has been fixed instead:

  OURS LOWER, because the call moved into a helper or a loop. ClearSprite and
  ReleaseSprite share a FreeSpriteContents; DrawSeqBar does its three fills
  through one SeqBarFill; ShutdownInput releases three devices through one
  ReleaseDevice, and StopAllSounds walks 73 slots through one StopIfPlaying.
  The calls are all there, just not once each in that function body.

  MERGED NEIGHBOURS -- no longer a reason, and worth saying so. This used to
  read "original higher, because docs/functions.tsv merges neighbours", which
  was true and was left as an excuse for years of noise. It cuts BOTH ways: a
  site in a reconstructed function attributed to the merged entry makes the
  original look emptier than it is, which is how RestoreTileSet came out 0
  against 2 and looked like a reconstruction inventing COM calls. Sites are now
  re-attributed to the real function through tools/merges.py, and that removed
  two of the thirteen disagreements outright.

  ORIGINAL LOWER, because comcalls.py cannot always find the vtable register.
  It scans backwards from the call and stops at a `ret`, correctly -- a block
  reached by a branch may have had the register loaded in a predecessor it
  cannot see. PresentFrame's first Flip is exactly that, and is real.

So a difference means "read this one", not "this one is broken". What it is
good for is the fourth case, which is a genuine defect: a reconstruction that
makes a call the original does not, or misses one it does.

Run it after a batch, the way tools/ab.sh is run after a batch.
"""

import collections
import csv
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import am2

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CRT_START = 0x0045C000


def addr_table():
    out = {}
    pat = re.compile(r"#define\s+(ADDR_[A-Z0-9_]+)\s+0x([0-9A-Fa-f]+)u?")
    with open(os.path.join(REPO, "src", "inject", "orig.h")) as fh:
        for line in fh:
            m = pat.match(line.strip())
            if m:
                out[m.group(1)] = int(m.group(2), 16)
    return out


def patched_functions(names):
    """{address: (C++ name, file)} for everything patch_replace installs."""
    game = os.path.join(REPO, "src", "game")
    pat = re.compile(r"patch_replace\(\s*(ADDR_[A-Z0-9_]+)\s*,\s*"
                     r"\(const void \*\)\s*(\w+)")
    out = {}
    for fn in sorted(os.listdir(game)):
        if not fn.endswith(".cpp"):
            continue
        with open(os.path.join(game, fn)) as fh:
            for m in pat.finditer(fh.read()):
                if m.group(1) in names:
                    out[names[m.group(1)]] = (m.group(2), fn)
    return out


def body_of(src, name):
    """The text of a function definition, from its opening brace to the first
    closing brace in column zero."""
    m = re.search(r"\n[A-Za-z_][^\n;]*?\b" + re.escape(name) + r"\s*\([^;{]*?\)\s*\{",
                  src, re.S)
    if not m:
        return None
    rest = src[m.end():]
    end = rest.find("\n}")
    return rest[:end] if end >= 0 else rest


def main():
    names = addr_table()
    patched = patched_functions(names)

    sizes = {int(r["addr"], 16): int(r["size"])
             for r in csv.DictReader(
                 open(os.path.join(REPO, "docs", "functions.tsv")), delimiter="\t")}

    # comcalls.tsv files each site under its functions.tsv entry, and those
    # entries merge neighbours -- so a site in a reconstructed function can be
    # attributed to the merged address instead, and this reports the
    # reconstruction as making COM calls its "original" does not. RestoreTileSet
    # read 0 against 2 for exactly that. merges.py knows where the real
    # boundaries are; re-attribute through it.
    import merges
    img = am2.Image()
    merged = merges.real_functions(img)

    per_fn = collections.Counter()
    with open(os.path.join(REPO, "docs", "comcalls.tsv")) as fh:
        for r in csv.DictReader(fh, delimiter="\t"):
            if r.get("abi") != "stdcall":
                continue            # a C++ virtual, or not decidable
            fn = int(r["func"], 16)
            if not fn or fn >= CRT_START:
                continue
            if fn in merged:
                starts, size = merged[fn]
                fn, _real_size = merges.owner(starts, size, int(r["site"], 16))
            per_fn[fn] += 1

    differ = []
    for va, (name, fname) in sorted(patched.items(), key=lambda kv: kv[1][0].lower()):
        src = open(os.path.join(REPO, "src", "game", fname)).read()
        body = body_of(src, name)
        if body is None:
            continue
        ours = len(re.findall(r"\bIDirect\w+_\w+\s*\(", body))
        theirs = per_fn.get(va, 0)
        if ours != theirs:
            differ.append((name, fname, theirs, ours, sizes.get(va, 0)))

    if not differ:
        print("every reconstruction makes as many COM calls as its original")
        return 0

    print(f"{len(differ)} of {len(patched)} disagree -- read them, do not assume\n")
    print(f"{'function':<24} {'file':<16} {'orig':>5} {'ours':>5}  likely reason")
    for name, fname, theirs, ours, size in differ:
        if ours < theirs:
            why = "a helper or a loop, or a merged neighbour"
        else:
            why = "comcalls lost the vtable register at a branch"
        print(f"{name:<24} {fname:<16} {theirs:>5} {ours:>5}  {why}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
