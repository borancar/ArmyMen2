"""Verify the numeric claims in CLAUDE.md against the tools that produce them.

Three separate figures in CLAUDE.md were found stale by measuring rather than
reading: the Lock/Unlock item counted two functions that do not lock, the COM
function count was one high, and an "unknown sites" figure described history as
if it were current. Each had the same cause -- a tool changed, some prose was
updated, and the rest went on asserting the old number.

`make check` already fails when a GENERATED file drifts. Prose cannot be
regenerated, so the next best thing is to state where each number comes from
and check it. That is what this does: every entry below pairs a sentence in
CLAUDE.md with the computation that produces its number.

It is deliberately small. Most of CLAUDE.md is judgement and prose, which is
not checkable and should not pretend to be -- the argument for keeping numbers
OUT of prose and pointing at docs/boundary.md instead. What is here is the
handful that kept going stale.

    tools/checkclaims.py      # exits non-zero if CLAUDE.md and the tools differ
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
CRT_START = 0x0045C000


def com_rows():
    with open(os.path.join(REPO, "docs", "comcalls.tsv")) as fh:
        return list(csv.DictReader(fh, delimiter="\t"))


def import_rows():
    with open(os.path.join(REPO, "docs", "imports.tsv")) as fh:
        return list(csv.DictReader(fh, delimiter="\t"))


def lock_bracket():
    """(functions calling LockSurface/UnlockSurface, how many are ours)."""
    img = am2.Image()
    done = merges.reconstructed()
    merged = merges.real_functions(img)
    sizes = {int(r["addr"], 16): int(r["size"])
             for r in csv.DictReader(
                 open(os.path.join(REPO, "docs", "functions.tsv")),
                 delimiter="\t")}
    seen = set()
    for _n, start, _e, data in img.sections:
        for j in range(len(data) - 5):
            if data[j] != 0xE8:
                continue
            t = start + j + 5 + struct.unpack_from("<i", data, j + 1)[0]
            if t not in (0x0041B9A0, 0x0041BA40):
                continue
            site = start + j
            fn = next((f for f, sz in sizes.items() if f <= site < f + sz), None)
            size = sizes.get(fn, 0)
            if fn in merged:
                starts, msz = merged[fn]
                fn, size = merges.owner(starts, msz, site)
            if fn:
                seen.add((fn, size))
    ours = sum(1 for f, sz in seen if any(f <= d < f + sz for d in done))
    return len(seen), ours


def module_split():
    """(flat modules, win32 modules) -- how src/game is divided.

    checksplit.py checks that each module is on the correct side; nothing
    checked how MANY were, and the prose describing the split was eleven
    modules out of date before anyone measured it.
    """
    srcs = am2.game_sources()
    win32 = [p for p in srcs
             if os.path.basename(os.path.dirname(p)) == "win32"]
    return len(srcs) - len(win32), len(win32)


def check_tool_count():
    """How many analysis tools `make check` actually runs.

    CLAUDE.md's own paragraph about this says it "said eight here for a long
    time after it stopped being eight, which is what a hand-maintained number
    in prose always comes to" -- and then said fifteen while the recipe ran
    seventeen. A warning about stale numbers is not a defence against one.
    """
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    text = open(os.path.join(root, "Makefile"), encoding="utf-8").read()
    m = re.search(r"^\tfor t in ([a-z0-9 ]+); do", text, re.M)
    if not m:
        raise SystemExit("checkclaims: cannot find the check recipe's tool list")
    return (len(m.group(1).split()),)


def claims():
    com = com_rows()
    game = [r for r in com if int(r["func"], 16) < CRT_START and r["func"] != "0x00000000"]
    imp = [r for r in import_rows() if int(r["func"], 16) < CRT_START]
    total, ours = lock_bracket()
    flat, win32 = module_split()

    return [
        ("in-game COM-shaped sites that are C++",
         r"\*\*(\d+) of (\d+) in-game sites are C\+\+ rather than COM\*\*",
         (sum(1 for r in game if r["abi"] == "thiscall"), len(game))),

        ("functions below the CRT touching the import table",
         r"outward-in: (\d+)\n  functions below the CRT touch the import table",
         (len({r["func"] for r in imp}),)),

        ("functions containing genuine COM dispatch",
         r"import table \(`docs/imports\.tsv`\) and (\d+)\n  contain genuine COM dispatch",
         (len({r["func"] for r in game if r["abi"] == "stdcall"}),)),

        ("total COM-shaped dispatch sites",
         r"`cdecl` for all (\d+) with nothing unknown",
         (len(com),)),

        ("modules under src/game/win32",
         r"talks to Win32 or DirectX -- \*\*(\d+)\*\*",
         (win32,)),

        ("modules in the flat half of src/game",
         r"touches no\s+API at all, and there are \*\*(\d+)\*\*",
         (flat,)),

        ("functions calling the Lock/Unlock bracket, and how many are ours",
         r"Measured: \*\*(\d+) functions\*\* call the bracket and \*\*(\d+)\*\*",
         (total, ours)),

        ("analysis tools `make check` runs",
         r"does not need the game\.\*\* \*\*(\d+)\*\* analysis",
         check_tool_count()),
    ]


def main():
    text = open(os.path.join(REPO, "CLAUDE.md")).read()
    bad = 0
    for what, pattern, expected in claims():
        m = re.search(pattern, text)
        if not m:
            print(f"  MISSING  {what}\n           no sentence matches {pattern!r}")
            bad += 1
            continue
        found = tuple(int(g) for g in m.groups())
        if found != expected:
            print(f"  STALE    {what}\n"
                  f"           CLAUDE.md says {found}, the tools say {expected}")
            bad += 1
        else:
            print(f"  ok       {what}: {expected}")

    if bad:
        print(f"\n{bad} claim(s) no longer true. Fix the sentence, or find out "
              "why a tool\nchanged its mind -- both have happened.")
        return 1
    print("\nEvery checked claim matches. Most of CLAUDE.md is judgement and "
          "cannot be\nchecked this way, which is the argument for keeping "
          "numbers in docs/boundary.md\nrather than in prose.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
