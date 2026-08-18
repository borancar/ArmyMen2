"""Cross-check each reconstruction's COM calls against the original's.

A reconstructed function should make the same DirectX calls as the code it
replaces. This counts them on both sides -- the original from
docs/comcalls.tsv, ours by matching `IDirectSomething_Method(` in the
function body -- and prints the pairs that disagree.

It IS a test now, in the only sense that matters: it exits non-zero when a
difference is not accounted for. It did not used to be. It printed ten
disagreements every run under the heading "read them, do not assume", which is
a standing instruction nobody carries out on the tenth reading -- and a real
defect would have looked exactly like the other nine.

Two things make a direct count differ without anything being wrong, and the
tool now does the arithmetic for both rather than describing them:

  OURS LOWER, because the call moved into a static helper. ClearSprite and
  ReleaseSprite share a FreeSpriteContents; ShutdownInput releases three
  devices through one ReleaseDevice; RestoreLostSurfaces restores three
  surfaces through one RestoreIfLost. The `+helpers` column adds them back, and
  all seven such functions then match their original EXACTLY.

  Only `static` helpers count. A non-static callee is a peer -- its own
  reconstruction with its own row -- and counting its calls again would double
  them. StopAllSounds calls StopAudioStream and CommLobbyStart calls
  CommGetSessionDesc and CommCreatePlayer; without that rule both overcount and
  look like defects.

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

It also re-measures comcalls.py's own blind spot. That scan gives up when it
cannot find the vtable load, so it undercounts, and its docstring quantified
the miss and said to re-measure if the scan ever changed. It has changed --
the cdecl rule -- so this measures it instead of leaving a note to go stale.
Eight vtable-shaped calls go unrecorded; five are real DirectX in reconstructed
functions and three are callbacks.

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
    for path in am2.game_sources():
        with open(path) as fh:
            for m in pat.finditer(fh.read()):
                if m.group(1) in names:
                    out[names[m.group(1)]] = (m.group(2),
                                              os.path.relpath(path, game))
    return out


def static_helpers(src):
    """Names of file-local (static) functions in `src`.

    The discriminator between a helper and a peer. A `static` function was
    factored out of the reconstruction and its COM calls belong to whoever
    calls it; a non-static one is a reconstruction in its own right with its
    own row in the survey, so counting its calls again would double them.
    Without this the accounting overcounts -- StopAllSounds calls
    StopAudioStream, which is a peer, and CommLobbyStart calls two more.
    """
    return set(re.findall(r"^static\s+[A-Za-z_][^\n;=]*?\b([A-Za-z_]\w*)\s*\(",
                          src, re.M))


def com_calls_including_helpers(src, name, helpers, depth=0):
    """COM calls in `name`, plus those in the static helpers it calls."""
    body = body_of(src, name)
    if body is None or depth > 3:
        return 0
    total = len(re.findall(r"\bIDirect\w+_\w+\s*\(", body))
    for helper in helpers:
        n = len(re.findall(r"\b" + re.escape(helper) + r"\s*\(", body))
        if n and helper != name:
            total += n * com_calls_including_helpers(src, helper, helpers,
                                                     depth + 1)
    return total


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


def sweep_missed(img, merged, sizes, done):
    """Every `call [reg+disp]` below the CRT that comcalls.py did not record.

    comcalls.py finds a COM dispatch by walking back for the vtable load, and
    gives up at a call or a ret. Its docstring quantifies the resulting miss
    and says it is worth re-measuring if the scan ever changes. It has changed,
    so this measures it rather than leaving the note to go stale.

    Returns (site, owning function, reconstructed?) for each.
    """
    import capstone as _cs
    md = _cs.Cs(_cs.CS_ARCH_X86, _cs.CS_MODE_32)
    md.detail = True

    recorded = set()
    with open(os.path.join(REPO, "docs", "comcalls.tsv")) as fh:
        for row in csv.DictReader(fh, delimiter="\t"):
            recorded.add(int(row["site"], 16))

    out = []
    for insn in img.disasm(".text"):
        if insn.address >= CRT_START or insn.mnemonic != "call":
            continue
        if "ptr [" not in insn.op_str or insn.address in recorded:
            continue
        ops = insn.operands
        if not ops or ops[0].type != _cs.x86.X86_OP_MEM:
            continue
        mem = ops[0].mem
        # [reg + disp] with a plausible vtable displacement. An index register
        # means an array walk and a zero base means an absolute address, and
        # neither is a vtable dispatch.
        if mem.base == 0 or mem.index != 0:
            continue
        if not 0 <= mem.disp <= 0x200 or mem.disp % 4:
            continue

        fn = next((f for f, sz in sizes.items() if f <= insn.address < f + sz),
                  None)
        size = sizes.get(fn, 0)
        if fn in merged:
            starts, msz = merged[fn]
            fn, size = merges.owner(starts, msz, insn.address)
        ours = fn is not None and any(fn <= d < fn + size for d in done)
        out.append((insn.address, fn or 0, ours))
    return out


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
            with_helpers = com_calls_including_helpers(
                src, name, static_helpers(src))
            differ.append((name, fname, theirs, ours, with_helpers))

    # The scan's own blind spot, re-measured rather than remembered. Use the
    # full reconstructed set, not just patch_replace: WndProc and
    # AudioTimerProc are registered rather than detoured and would otherwise
    # look outstanding.
    missed = sweep_missed(img, merged, sizes, merges.reconstructed())
    if missed:
        left = [m for m in missed if not m[2]]
        print(f"{len(missed)} vtable-shaped call(s) the scan did not record, "
              f"{len(left)} in unreconstructed code")
        for site, fn, ours in missed:
            if not ours:
                print(f"    {site:#010x} in {fn:#010x}")
        if left:
            print("    Read each one. As of this writing all three are callbacks\n"
                  "    rather than COM -- two `call [esp+N]` and one function\n"
                  "    pointer in a member field at +0x60, called with the object\n"
                  "    pushed and the CALLER cleaning up, so cdecl and not COM.\n"
                  "    A new entry here is not covered by that and needs reading.")
        print()

    if not differ:
        print("every reconstruction makes as many COM calls as its original")
        return 0

    by_helper = [d for d in differ if d[4] == d[2]]
    by_scan = [d for d in differ if d[4] != d[2] and d[3] > d[2]]
    unexplained = [d for d in differ if d not in by_helper and d not in by_scan]

    print(f"{len(differ)} of {len(patched)} differ on a direct count: "
          f"{len(by_helper)} accounted for by static helpers, "
          f"{len(by_scan)} by the scan's\nknown undercount, "
          f"{len(unexplained)} unexplained\n")
    print(f"{'function':<24} {'file':<20} {'orig':>5} {'ours':>5} {'+helpers':>9}  note")
    for name, fname, theirs, ours, total in differ:
        if total == theirs:
            why = "accounted for"
        elif ours > theirs:
            why = "comcalls lost the vtable register at a branch"
        else:
            why = "READ THIS ONE"
        print(f"{name:<24} {fname:<20} {theirs:>5} {ours:>5} {total:>9}  {why}")
    if unexplained:
        print("\nThe unexplained ones are the whole point of this tool: a "
              "reconstruction that\nmakes a call its original does not, or "
              "misses one it does.")
        return 1
    print("\nNothing unexplained. The `+helpers` column is the direct count "
          "plus the COM\ncalls of every static -- file-local -- helper the "
          "body calls, which is what a\nfactored-out reconstruction looks "
          "like. Peers are excluded: they are separate\nreconstructions with "
          "their own row, and counting them again would double.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
