"""Which trace counters can never move, and which mean what they say.

A counter of 0 does not mean "never called". The counters ARE the patch stubs,
so when one reconstructed function calls another directly the call never
crosses the patched entry and the callee's counter stays at 0 however often it
runs. CLAUDE.md has said so since early on.

Saying so has not been enough. The zero has been misread as "unexercised" at
least three times: WaveCloseReadFile was on a list of things to try harder at
when StopAudioStream had been calling it all along, and MovieDrawFrame,
MovieApplyPalette and SnapshotSystemPalette were written up in one commit as
proof the intro no longer plays the movie -- which it does, 200 frames at a
time.

The distinction is computable, so it should be computed rather than remembered.
For every reconstructed function this asks who calls it IN THE ORIGINAL IMAGE:

  BLIND      every caller is itself reconstructed, so the counter is 0 by
             construction and carries no information at all. Resolve these
             with a temporary probe.
  MEANINGFUL at least one caller is still the original's code, so the counter
             moves if the function is reached, and a 0 really is "not reached
             on this run".
  ENTRY      nothing CALLS it: it is reached by address, as a registered
             button handler or a vtable slot. Those counters do move -- the
             dispatcher invoking them is the game's own code -- so a 0 means
             the handler was not invoked. Listed separately only because the
             reason a caller cannot be found is different.

    tools/blindspots.py           # the summary
    tools/blindspots.py --blind   # just the ones whose counters cannot move
"""

import csv
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import am2
import merges

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def addr_names():
    """{address: ADDR_NAME} from orig.h."""
    out = {}
    pat = re.compile(r"#define\s+(ADDR_[A-Z0-9_]+)\s+0x([0-9A-Fa-f]+)u?")
    with open(os.path.join(REPO, "src", "inject", "orig.h")) as fh:
        for line in fh:
            m = pat.match(line.strip())
            if m:
                out.setdefault(int(m.group(2), 16), m.group(1))
    return out


def patch_labels():
    """{address: the name the harness traces it under}."""
    names = {}
    pat = re.compile(r"#define\s+(ADDR_[A-Z0-9_]+)\s+0x([0-9A-Fa-f]+)u?")
    with open(os.path.join(REPO, "src", "inject", "orig.h")) as fh:
        for line in fh:
            m = pat.match(line.strip())
            if m:
                names[m.group(1)] = int(m.group(2), 16)

    out = {}
    game = os.path.join(REPO, "src", "game")
    call = re.compile(r'patch_replace\(\s*(ADDR_[A-Z0-9_]+)\s*,[^,]*,\s*"([^"]+)"')
    for fn in sorted(os.listdir(game)):
        if fn.endswith(".cpp"):
            with open(os.path.join(game, fn)) as fh:
                for m in call.finditer(fh.read()):
                    if m.group(1) in names:
                        out[names[m.group(1)]] = m.group(2)
    return out


def call_sites(img):
    """{target: [site, ...]} for every call OR jump that enters a function.

    Tail calls count. PollInput is `call PollMouse; jmp PollKeyboard`, so a scan
    that reads only `call rel32` finds no caller for PollKeyboard and files it
    as an unreferenced entry point -- when its counter demonstrably moves,
    3,726 times in a title-screen run.

    This is the third time that gap has cost something here: the same omission
    recorded 0x0042ECF0 as dead code in 44312d2, and nearly did the same for
    0x004256F0 while the level teardown was being traced. Scan both opcodes.
    """
    out = {}
    for _name, start, _end, data in img.sections:
        for j in range(len(data) - 5):
            if data[j] in (0xE8, 0xE9):
                t = start + j + 5 + struct.unpack_from("<i", data, j + 1)[0]
                out.setdefault(t, []).append(start + j)
    return out


def main():
    img = am2.Image()
    done = merges.reconstructed()
    labels = patch_labels()
    names = addr_names()
    sizes = {int(r["addr"], 16): int(r["size"])
             for r in csv.DictReader(
                 open(os.path.join(REPO, "docs", "functions.tsv")),
                 delimiter="\t")}
    merged = merges.real_functions(img)
    calls = call_sites(img)

    def owner(site):
        fn = next((f for f, sz in sizes.items() if f <= site < f + sz), None)
        if fn in merged:
            starts, size = merged[fn]
            fn, _ = merges.owner(starts, size, site)
        return fn

    rows = []
    for addr in sorted(labels):
        callers = calls.get(addr, [])
        if not callers:
            kind = "entry"
        elif all(owner(c) in done for c in callers):
            kind = "blind"
        else:
            kind = "meaningful"
        rows.append((kind, labels[addr], addr, len(callers)))

    if "--blind" in sys.argv:
        blind = [r for r in rows if r[0] == "blind"]
        print("counters that CANNOT move -- a 0 here means nothing, and only a "
              "probe will\ntell you whether the function ran:\n")
        for _kind, name, addr, n in sorted(blind, key=lambda r: r[1].lower()):
            print(f"  {name:<26} {addr:#010x}  all {n} caller(s) reconstructed")

        entry = [r for r in rows if r[0] == "entry"]
        if entry:
            print("\nreached by address rather than called -- a registered "
                  "handler or a vtable\nslot. These counters DO move when the "
                  "handler fires, because the dispatcher\nis the game's own "
                  "code; a 0 means it was not invoked:\n")
            for _kind, name, addr, _n in sorted(entry, key=lambda r: r[1].lower()):
                print(f"  {name:<26} {addr:#010x}")
        return 0

    print(f"{len(rows)} traced functions: "
          f"{sum(1 for r in rows if r[0] == 'meaningful')} counters mean what "
          f"they say,\n{sum(1 for r in rows if r[0] == 'blind')} are blind "
          f"because every caller is ours, and "
          f"{sum(1 for r in rows if r[0] == 'entry')} are reached by address.\n")
    print("Run with --blind for the lists. A 0 on a blind counter is not "
          "evidence of\nanything; resolve it with a temporary probe, which is "
          "what CLAUDE.md has always\nsaid and what keeps being forgotten.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
